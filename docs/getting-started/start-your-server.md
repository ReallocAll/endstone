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

At startup, the launcher adds missing defaults to `endstone.toml` while keeping your existing values and comments.
This applies to the whole configuration, including [mob AI settings](../tutorials/mob-ai.md) when the controls are
disabled.

If the launcher reports a TOML error, correct `endstone.toml` and start the server again. If a configuration update
fails, your original file is kept; resolve the reported file or permission problem and retry.

??? note "Shared files and custom permissions"
    The launcher cannot update a configuration with multiple hard links. Use a symlink to share one configuration
    between paths. Updates preserve ordinary POSIX permissions, but do not copy custom ACLs, extended attributes or
    Windows security metadata; reapply these if your deployment needs them.


[installed]: installation.md

[Bedrock Dedicated Server]: https://www.minecraft.net/en-us/download/server/bedrock
