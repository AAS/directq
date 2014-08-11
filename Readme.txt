DirectQ Readme
==============

Not so much a readme as a list of things I think you need to know...

Update your DirectX.  You should have the Feb 2010 release for DirectQ; earlier versions may also work but I make no
guarantees.  This also applies to Vista and Windows 7 users.

DirectX 9 class hardware (or better) gives the best results.  It should also work on a wide range of older hardware, but
the older the hardware is the more likely it is that you will have issues.

Tuned for high performance - there is no need to disable multitexture, run in 16-bit colour or any other performance hacks
you might need from GLQuake.

Works on Windows 2000, XP, Vista and 7 without needing any compatibility settings.  Works cleanly on 64-bit and multicore
PCs.

Bugs should be reported to http://mhquake.blogspot.com.  When reporting a bug, please cross-check it against WinQuake or
GLQuake first.  Ensure that it's not a content bug.  Give me enough information to reproduce it.  Tell me what error
messages (if any) you get.  Tell me what the problem is, not what you think the solution is.

r_fastlightmaps 3 can give increased performance with little or no visual degradation.

GeForce FX users should set r_hlsl 0 or they may experience poor performance in scenes with lots of water and/or sky.

gl_subdivide_size can now be changed without restarting the map; rendering optimizations make lower values possible.  Start
at 24 and work up if you need to.  Does not apply to r_hlsl 1 mode which does not subdivide.

