# ps1-simple-example-3d-game
a simple ps1 example of a 3d-game

work in progress

thank you to:
 - spicyjpeg - https://github.com/spicyjpeg/ps1-bare-metal (love the baremetal lib, and thank you for answering questions on discord, its very awesome)
 - lameguy64 - https://github.com/Lameguy64/PSn00bSDK
 - bandwidth - https://www.youtube.com/@Bandwidth_ytb (I didn't know this sort of thing was possible until I came across your videos) 
 - any one else I am forgetting that answered questions, thank you as well

setup for windows (could probably be easily adjusted for linux with a proper sh script and equivlent linux tool chains)
 - install compiler and multi-arch gdb
    - https://static.grumpycoder.net/pixel/gdb-multiarch-windows/
        - gdb-multiarch-16.3.zip (what I used)
    - https://github.com/Lameguy64/PSn00bSDK/releases
        - gcc-mipsel-none-elf-12.3.0-windows.zip (what I used)
        - add the unzipped folder to your PATH env variable so you dont need to qualify the full path when calling it
 - install pcsx-redux emulator for debug server stuff
    - (use link here to install, https://www.psx.dev/getting-started)
    - you will need to enable the gdb server (I used port 3002)
    - then you will need a .vscode/launch.json setup like this so you can lunch the debug client
        - notice, points at game.elf, not game.psexe

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Attach to PCSX-Redux (gdbserver :3002)",
            "type": "cppdbg",
            "request": "launch",
            "program": "${workspaceFolder}\\build\\game.elf",
            "cwd": "${workspaceFolder}",
            "MIMode": "gdb",
            "miDebuggerPath": "path_to_tools\\gdb-multiarch-16.3\\bin\\gdb-multiarch.exe",
            "miDebuggerServerAddress": "127.0.0.1:3002",
            "setupCommands": [
                { "text": "-enable-pretty-printing" },
                { "text": "-gdb-set disassembly-flavor intel" }
            ]
        }
    ]
}
```

to build just call "build" from cmd in the project folder

if you add any .c files be sure to edit the bat script, they need to go into the sources.rsp file (I am not clever enough to do it automatically in cmd, also, cmd is the worst shell language by far, I have ever come across; If shell scripting is one of Microsoft's love languages, then they are all full of hate...just saying)