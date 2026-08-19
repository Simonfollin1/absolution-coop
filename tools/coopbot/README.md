# coopbot — a second player without a second machine

`coopbot.py` speaks the mod's wire protocol and joins a hosted session like any
other player. It walks in a slow circle beside you, shows up in the panel and
the world overlay, announces a scene so the **Go there** button lights up,
drops markers, reaches checkpoints and dies on command. Everything a friend
would do to your session, minus the friend.

It needs Python 3 and nothing else. Run it on the same PC as the game.

## Quick start

1. Start the game, `F6`, **Host**.
2. In a terminal next to `coopbot.py`:

   ```
   py coopbot.py 127.0.0.1 --name Bot
   ```

3. The panel now lists Bot, and the avatar orbits you in the world.

Everything the bot sees and does is appended to `coopbot.log`.

## Making the Go there button appear

The button shows when a peer is in a mission you are not in. Sit in the main
menu (or any other level) and start the bot with a scene:

```
py coopbot.py 127.0.0.1 --name Bot --scene "assembly:/path_to_scene.entity" --checkpoint 0
```

Scene paths are in `mods/Coop/src/Game/SceneTable.cpp`, one per level. The bot
repeats the announcement every three seconds, exactly like the mod does.

## Commands

Type at the bot's prompt once connected:

```
say <text>           chat line
marker [x y z]       drop a marker (default: where the bot is standing)
checkpoint <n>       announce reaching checkpoint n
scene <path> [n]     announce being in a scene; n = checkpoint
scene off            stop announcing
die / revive         set or clear the dead flag (die also sends the death note)
level <a> [b]        chapter / section to claim when nobody is shadowed
where <x> <y> <z>    orbit centre when nobody is shadowed
offset <x> <y> <z>   how far from the shadowed player to walk
shadow on|off        follow the freshest real player, or stay put
loading on|off       set or clear the loading flag
peers                who the bot can see, and how fresh
rtt                  last measured round trip
leave                say goodbye and exit
```

By default the bot **shadows** you: it copies your chapter and section and
orbits 2.5 m from wherever your last position packet said you were, which is
what makes the in-world avatar drawing testable alone.

## Keeping it honest

The wire format is duplicated in Python by hand, so the build compiles the
mod's real codec (`goldengen.cpp` + `mods/Coop/src/Net/Protocol.cpp`), prints
one golden sample of every message, and requires
`coopbot.py --selftest --golden golden.txt` to produce the identical bytes.
A protocol change that forgets one side fails the build instead of a session.
