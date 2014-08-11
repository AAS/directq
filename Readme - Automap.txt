The DirectQ automap has been updated for this release to be more usable and give better performance under all but the most adverse conditions.  The following commands and cvars are used to interact with it.

"toggleautomap" (command)
-------------------------
Switches the automap on or off.  You should bind a key to it (it's been added to the Customize Controls menu to make this easier) for your own convenience.  Switching the automap on will pause the game client-side, the same way as the menu or console does.  When it is switched on it will only accept a limited subset of keys: navigation keys (see below), whichever key has been bound to "toggleautomap", whichever key has been bound to "screenshot" and the ESC key, which will toggle the automap off (in case you forgot to bind a key).

"r_automapscroll_x" and "r_automapscroll_y" (cvars, default to -64)
--------------------------------------------------------------------
Automap navigation uses the arrow keys to scroll and the Home and End keys to zoom in and out.  By default the sense for the arrow keys is inverted; if this bothers you then you can change these cvars to positive numbers.  They can also be changed to lower or higher numbers if you'd like to adjust the scroll speed.

"scr_automapinfo" (cvar, default to 3)
--------------------------------------
When the automap is drawn a certain amount of information is displayed above and below it.  Setting this cvar to 0 will disable the display of that information.  Set it to 1 for the bottom information only, or to 2 for the top information only.

"r_automap_nearclip" (cvar, default 48)
---------------------------------------
Used to control the positioning of the near clipping plane when drawing the automap; anything higher than this amount of units above the player won't be drawn.

"scr_automapposition" (cvar, default 1)
---------------------------------------
An indicator is drawn to show the players position on the automap; set this to 0 to turn it off.

