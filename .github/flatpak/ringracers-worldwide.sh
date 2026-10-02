#!/bin/bash
# Launcher for the Ring Racers Worldwide Flatpak: the official Flatpak's
# launcher (ringracers.sh on Flathub), with the data looked for outside.

for i in {0..9}; do
	test -S $XDG_RUNTIME_DIR/discord-ipc-$i || ln -sf {app/com.discordapp.Discord,$XDG_RUNTIME_DIR}/discord-ipc-$i;
done

# This build carries no game data. It reads the 2.4 data of the official
# Flatpak (org.kartkrew.RingRacers, Flathub) when it is installed -- for this
# user first, then system-wide -- or the folder RINGRACERSWADDIR names, which
# this app must be allowed to read:
#   flatpak override --user --filesystem=/path/to/data:ro io.github.ringracers_worldwide.RingRacersWorldwide
if [ -z "${RINGRACERSWADDIR}" ]; then
	# Not $XDG_DATA_HOME: in the sandbox, it is this app's own data folder.
	for dir in \
		"$HOME/.local/share/flatpak/app/org.kartkrew.RingRacers/current/active/files/ringracers-data" \
		"/var/lib/flatpak/app/org.kartkrew.RingRacers/current/active/files/ringracers-data"; do
		if [ -d "$dir" ]; then
			export RINGRACERSWADDIR="$dir"
			break
		fi
	done
fi

if [ -z "${RINGRACERSWADDIR}" ]; then
	echo "Ring Racers Worldwide: no game data found. Install Dr. Robotnik's Ring Racers" >&2
	echo "from Flathub (org.kartkrew.RingRacers), or set RINGRACERSWADDIR to a folder" >&2
	echo "holding the 2.4 data and allow this app to read it (flatpak override)." >&2
fi

exec ringracers "$@"
