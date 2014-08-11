DIRECT 3D QUAKE!
================

What is it?
-----------
It's a true native Direct 3D 9.0c port of GLQuake.  It doesn't use the gl_fakegl wrapper, or any other wrapper, but is rather a complete rewrite of GLQuake's renderer to use proper Direct 3D.


Why is it?
----------
A number of reasons.  The prevailing wisdom is that Direct 3D is foisted on us by evil alien overlords, so there was an element of perversity, I suppose.  I was also at the stage where I was sufficiently frustrated with OpenGL to make the switch.  I don't really like the direction OpenGL seems to be going in at the present moment, it reminds me a lot of the dying throes of 3DFX moreso than anything else, so that was another factor.

Another consideration was the engines already out there.  There are - it seems to me - 3 main contenders.  ProQuake for the ultra-trads, QRack for eye candy, and DarkPlaces for everything *including* the kitchen sink (with extra kitchen sink).  However, there is a gap in the market, and that's for the people with Intel and ATI cards who often struggle to get OpenGL based engines working correctly.

I'm not arrogantly thinking I'm going to fill that gap, this engine is purely for my own personal amusement first and foremost, but if it helps anyone else get enjoyment out of a 12 year old game it's a good thing too.


What version of Direct 3D is needed?
------------------------------------
It should run on any Direct 3D 9.0c card, but it practice there are a few it probably won't run on.  I coded using the March 2008 SDK, so if you get an error saying that a required DLL is missing, you'll need to update your installed version of DirectX.


What else is needed?
--------------------
If you can run GLQuake you can probably also run this.  Direct3D has a few features, like texture locking and vertex buffers, that allow this engine to more memory-efficient than GLQuake was, so even if you can't run GLQuake you might still be able to run this.


How fast is it?
---------------
About the same speed as GLQuake.  I don't currently have a stock GLQuake to compare with, but on an Intel 915 mobile chipset I'm getting over 120 FPS in timedemo demo1, with a debug build.


What's new in it?
-----------------
Not much really.  The initial goal was to just get a straight port of stock GLQuake functionality working, and this is it.  I have removed a few things, like mirrors and gl_flashblend 1 mode.  There's no interpolation and I haven't renamed or removed any of the gl_* cvars, so as not to mess with people's configs.  Maybe some small bugfixes, but overall nothing much beyond what was in stock GLQuake.

I did add in 64 bit lightmaps, as they were really easy to do, and allowed me to significantly raise the cap on the maximum light intensity in the game (quadrupled, with better granularity than stock GLQuake, so stair-steps on them are vastly reduced).  I also removed the hard limits on the number of lightmaps, textures and video modes you can have.

Future releases may add extra functionality.
