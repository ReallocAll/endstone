#include "bedrock/world/actor/ai/goal/mob_ai_goals.h"

#ifdef __linux__

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>

#include "bedrock/entity/components/actor_data_flag_component.h"
#include "bedrock/entity/components/actor_game_type_component.h"
#include "bedrock/entity/components/actor_type_flag_component.h"
#include "bedrock/entity/components/actor_unique_id_component.h"
#include "bedrock/entity/components/dimension_type_component.h"
#include "bedrock/entity/components/offsets_component.h"
#include "bedrock/entity/components/state_vector_component.h"
#include "bedrock/world/actor/actor.h"
#include "bedrock/world/actor/player/player.h"
#include "bedrock/world/actor/provider/actor_riding.h"
#include "bedrock/world/level/dimension/vanilla_dimensions.h"
#include "bedrock/world/level/level.h"
#include "endstone/core/perf/mob_ai_capabilities.h"
#include "endstone/core/server.h"
#include "endstone/runtime/hook.h"
#include "endstone/runtime/mob_ai_preflight.h"

namespace {
using endstone::core::EndstoneServer;
using endstone::core::MobAiState;
using endstone::core::mobAiWithinRange;
using endstone::core::supportsMobAiIdle;
using endstone::core::supportsMobAiSearch;

Actor *owner(void *goal, const endstone::runtime::MobAiGoalLayout &layout)
{
    if (!MobAiState::isNativeAvailable()) {
        return nullptr;
    }
    void *actual_vtable;
    std::memcpy(&actual_vtable, goal, sizeof(actual_vtable));
    if (actual_vtable != static_cast<char *>(endstone::runtime::get_executable_base()) + layout.vtable) {
        return nullptr;
    }
    Actor *actor;
    std::memcpy(&actor, static_cast<char *>(goal) + layout.owner_offset, sizeof(actor));
    return actor;
}

bool preventSleep(Actor *actor)
{
    if (!actor || actor->getActorIdentifier().getCanonicalName() != "minecraft:villager_v2") {
        return false;
    }
    const auto *id = actor->tryGetComponent<ActorUniqueIDComponent>();
    if (!id || id->unique_id.raw_id == -1) {
        return false;
    }
    auto &server = EndstoneServer::getInstance();
    return server.getMobAiPolicies().resolve("minecraft:villager_v2").prevent_sleep;
}

void clearSearch(const Actor *actor)
{
    if (actor) {
        if (const auto *id = actor->tryGetComponent<ActorUniqueIDComponent>(); id && id->unique_id.raw_id != -1) {
            EndstoneServer::getInstance().getMobAiState().clearSearch(id->unique_id.raw_id);
        }
    }
}

bool usableMob(const Actor *actor)
{
    if (!actor || actor->isRemoved() || actor->isClientSide() || actor->isPlayer()) {
        return false;
    }
    const auto *id = actor->tryGetComponent<ActorUniqueIDComponent>();
    const auto *type = actor->tryGetComponent<ActorTypeComponent>();
    return id && id->unique_id.raw_id != -1 && type && (type->type & ActorType::Mob) == ActorType::Mob &&
           type->type != ActorType::Player && actor->tryGetComponent<ActorDataFlagComponent>();
}

std::optional<std::array<double, 3>> feetPosition(const Actor &actor)
{
    const auto *position = actor.tryGetComponent<StateVectorComponent>();
    const auto *offset = actor.tryGetComponent<OffsetsComponent>();
    if (!position || !offset || !std::isfinite(position->pos.x) || !std::isfinite(position->pos.y) ||
        !std::isfinite(position->pos.z) || !std::isfinite(offset->height_offset)) {
        return std::nullopt;
    }
    const std::array<double, 3> feet{position->pos.x, static_cast<double>(position->pos.y) - offset->height_offset,
                                     position->pos.z};
    return std::isfinite(feet[1]) ? std::optional(feet) : std::nullopt;
}

bool activeMob(const Actor &actor, std::int64_t id, double range, MobAiState &state)
{
    if (actor.getTargetId().raw_id != -1 || actor.isRiding() ||
        !ActorRiding::getPassengers(actor.getEntity()).empty() ||
        state.recentlyActive(id, actor.getLevel().getCurrentServerTick().tick_id)) {
        return true;
    }
    constexpr std::array protected_flags{
        ActorFlags::LEASHED,        ActorFlags::INLOVE,    ActorFlags::TEMPTED,
        ActorFlags::TAMED,          ActorFlags::ANGRY,     ActorFlags::SCARED,
        ActorFlags::ONFIRE,         ActorFlags::USINGITEM, ActorFlags::TRADE_INTEREST,
        ActorFlags::IS_IN_UI,       ActorFlags::ADMIRING,  ActorFlags::CASTING,
        ActorFlags::CHARGING,       ActorFlags::IGNITED,   ActorFlags::FACING_TARGET_TO_RANGE_ATTACK,
        ActorFlags::DELAYED_ATTACK, ActorFlags::RAM_ATTACK};
    const auto *flags = actor.tryGetComponent<ActorDataFlagComponent>();
    for (const auto flag : protected_flags) {
        if (flags->getStatusFlag(flag)) {
            return true;
        }
    }
    const auto position = feetPosition(actor);
    const auto *dimension = actor.tryGetComponent<DimensionTypeComponent>();
    if (!position || !dimension || dimension->type == VanillaDimensions::Undefined) {
        return true;
    }
    bool active = false;
    auto visitor = [&](const Player &player) {
        if (player.isRemoved()) {
            return true;
        }
        const auto *player_dimension = player.tryGetComponent<DimensionTypeComponent>();
        if (player_dimension && player_dimension->type != VanillaDimensions::Undefined &&
            player_dimension->type != dimension->type) {
            return true;
        }
        const auto *game_type = player.tryGetComponent<ActorGameTypeComponent>();
        if (game_type && game_type->game_type == GameType::Spectator) {
            return true;
        }
        if (!player_dimension || player_dimension->type == VanillaDimensions::Undefined || !game_type ||
            player.isClientSide()) {
            active = true;
            return false;
        }
        switch (game_type->game_type) {
        case GameType::Survival:
        case GameType::Default:
            switch (actor.getLevel().getDefaultGameType()) {
            case GameType::Survival:
            case GameType::Creative:
            case GameType::Adventure:
                break;
            default:
                active = true;
                return false;
            }
            break;
        case GameType::Creative:
        case GameType::Adventure:
            break;
        default:
            active = true;
            return false;
        }
        const auto player_position = feetPosition(player);
        const auto nearby = player_position ? mobAiWithinRange(*position, *player_position, range) : std::nullopt;
        active = !nearby || *nearby;
        return !active;
    };
    actor.getLevel().forEachPlayer(std::ref(visitor));
    return active;
}

bool suppressIdle(Actor *actor, bool stroll)
{
    if (!usableMob(actor)) {
        clearSearch(actor);
        return false;
    }
    const auto &type = actor->getActorIdentifier().getCanonicalName();
    if (!supportsMobAiIdle(type, actor->tryGetComponent<ActorTypeComponent>()->type)) {
        clearSearch(actor);
        return false;
    }
    const auto id = actor->tryGetComponent<ActorUniqueIDComponent>()->unique_id.raw_id;
    auto &server = EndstoneServer::getInstance();
    const auto decision = server.getMobAiPolicies().resolve(type);
    const auto &policy = decision.animal;
    if (!policy || !(stroll ? policy->random_stroll : policy->random_look_around)) {
        return false;
    }
    auto &state = server.getMobAiState();
    if (activeMob(*actor, id, policy->activation_range_blocks, state)) {
        if (supportsMobAiSearch(type, actor->tryGetComponent<ActorTypeComponent>()->type, decision) &&
            (*decision.target_search_player_range_blocks == policy->activation_range_blocks ||
             activeMob(*actor, id, *decision.target_search_player_range_blocks, state))) {
            state.clearSearch(id);
        }
        return false;
    }
    return true;
}
}  // namespace

bool SleepGoal::canUse()
{
    const bool prevent = preventSleep(owner(this, endstone::runtime::sleep_goal_layout));
    const bool result = ENDSTONE_HOOK_CALL_ORIGINAL(&SleepGoal::canUse, this);
    return result && !prevent;
}

bool SleepGoal::canContinue()
{
    auto *actor = owner(this, endstone::runtime::sleep_goal_layout);
    if (preventSleep(actor)) {
        actor->setStatusFlag(ActorFlags::WANTS_TO_WAKE, true);
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&SleepGoal::canContinue, this);
}

bool RandomStrollGoal::canUse()
{
    if (suppressIdle(owner(this, endstone::runtime::random_stroll_goal_layout), true)) {
        return false;
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&RandomStrollGoal::canUse, this);
}

bool RandomStrollGoal::canContinue()
{
    if (suppressIdle(owner(this, endstone::runtime::random_stroll_goal_layout), true)) {
        return false;
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&RandomStrollGoal::canContinue, this);
}

bool RandomLookAroundGoal::canUse()
{
    if (suppressIdle(owner(this, endstone::runtime::random_look_around_goal_layout), false)) {
        return false;
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&RandomLookAroundGoal::canUse, this);
}

bool RandomLookAroundGoal::canContinue()
{
    if (suppressIdle(owner(this, endstone::runtime::random_look_around_goal_layout), false)) {
        return false;
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&RandomLookAroundGoal::canContinue, this);
}

ActorUniqueID NearestAttackableTargetGoal::_findTarget(const MobDescriptor **matched_definition)
{
    const auto *actor = owner(this, endstone::runtime::nearest_attackable_target_goal_layout);
    if (!usableMob(actor)) {
        clearSearch(actor);
    }
    else {
        const auto id = actor->tryGetComponent<ActorUniqueIDComponent>()->unique_id.raw_id;
        const auto &type = actor->getActorIdentifier().getCanonicalName();
        auto &server = EndstoneServer::getInstance();
        auto &state = server.getMobAiState();
        const auto decision = server.getMobAiPolicies().resolve(type);
        if (!supportsMobAiSearch(type, actor->tryGetComponent<ActorTypeComponent>()->type, decision) ||
            activeMob(*actor, id, *decision.target_search_player_range_blocks, state)) {
            state.clearSearch(id);
        }
        else if (!state.allowNewTargetSearch(id, actor->getLevel().getCurrentServerTick().tick_id,
                                             decision.target_search_interval_ticks)) {
            *matched_definition = nullptr;
            return ActorUniqueID::INVALID_ID;
        }
    }
    return ENDSTONE_HOOK_CALL_ORIGINAL(&NearestAttackableTargetGoal::_findTarget, this, matched_definition);
}

#endif
