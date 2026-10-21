---
comments: true
---

# Start your server

After you've [installed] Endstone, you can bootstrap your server using the `endstone` executable. Go to the directory
where you want your server to be located and enter:

```
endstone
```

Alternatively, if you're running Endstone from within Docker, use:

=== ":fontawesome-brands-linux: Linux / :fontawesome-brands-windows: Powershell"

    ```
    docker run --rm -it -v ${PWD}:/home/endstone -p 19132:19132/udp endstone/endstone
    ```

=== ":fontawesome-brands-windows: Command Prompt"

    ```
    docker run --rm -it -v "%cd%":/home/endstone -p 19132:19132/udp endstone/endstone
    ```

You should see this in your console:

![Start your server](start-your-server.png)

!!! tip
    The first time you run the bootstrap, it will need to download the [Bedrock Dedicated Server] from the official
    mirror. Press ++y++ and ++enter++ to continue.

## Configuration updates

At startup, the launcher adds missing defaults to `endstone.toml` while preserving existing values and comments.
This applies to the whole configuration, including when [mob AI controls](../tutorials/mob-ai.md) are disabled.
Files that need no changes are left alone. A changed file is replaced atomically at its resolved destination,
so relative symlinks remain intact, but the target's inode changes. A required update stops startup if the target
has multiple hard links; use a symlink when several paths should share the configuration.

On POSIX, replacement preserves the existing user ID, group ID and mode bits; new files use mode `0600`.
The launcher does not copy custom ACLs, extended attributes or Windows file security metadata from the old file.
Deployments that set these separately should account for replacement during configuration updates.
If writing or replacement fails, the original file is kept.


[installed]: installation.md

[Bedrock Dedicated Server]: https://www.minecraft.net/en-us/download/server/bedrock
