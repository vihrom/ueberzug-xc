ueberzug-xc
===========
A lightweight, high-performance X11 image drawing tool written in pure C. It acts as a drop-in replacement for the original Ueberzug tool.


Dependencies
------------
The project is self-contained and only requires the core X11 development library.


Installation
------------
To compile the binary and install it along with a `ueberzug` symlink into `/usr/local/bin`:

	make clean install

To customize the compiler, paths, or flags, edit `config.mk` before building.


Integration with Ranger
------------------------
Since `ueberzug-xc` installs a `ueberzug` symlink into your `$PATH`, it works out of the box with the Ranger file manager without any external wrapper scripts.

Just enable the `ueberzug` preview method in your `~/.config/ranger/rc.conf`:

	set preview_images true
	set preview_images_method ueberzug


Integration with lf
------------------------------
The `scripts/` directory contains ready-to-use configuration files and scripts for the `lf` file manager. 

1. Copy the preview scripts to your `lf` configuration folder:

	```bash
	cp scripts/preview.sh scripts/cleaner.sh ~/.config/lf/
	chmod +x ~/.config/lf/preview.sh ~/.config/lf/cleaner.sh
	```

2. Append the contents of `scripts/lfrc` to your `~/.config/lf/lfrc`:

	```bash
	cat scripts/lfrc >> ~/.config/lf/lfrc
	```

Other file managers
-------------------
Integration with yazi and nnn works, but window coordinates and rendering can be a bit quirky. The issue is known — we'll fix it if we ever get around to it (which is not guaranteed, feeling lazy for now).


Using inside tmux
-----------------
Since `ueberzug-xc` relies on X11 window placement and handles, it needs to access the correct environment variables from inside your terminal multiplexer.

Add the following line to your `~/.tmux.conf` to ensure environment variables are properly forwarded when creating new panes or windows:

	set -g update-environment "DISPLAY WINDOWID SSH_CONNECTION XAUTHORITY"
