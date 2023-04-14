Badank, the dankest baduk tournament manager!


What it is
----------

Badank is a simple tournament manager, comparable to twogtp.
It can let 2 or more Go/Baduk programs battle (as long as they "speak" GTP). On a multicore system, it
can multiple games in parallel.
The result is a an sgf-file (for further analysis) and a pgn-file. The pgn-file only contains the
results and is created for processing with BayesElo and/or Ordo.

I use it myself to verify if a change to my Go program made it play better (or worse) ELO-wise.


How to build
------------

Required: cmake & libconfig++-dev

* mkdir build
* cd build
* cmake ..
* make


Configuration
-------------

Configuration takes place in badank.cfg. Check it; you'll figure it out yourself most likely.

You can select a different configuration-file by adding it to the command line.



(c) 2021-2023 by Folkert van Heusden <mail@vanheusden.com>

Released under MIT license.
