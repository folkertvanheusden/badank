What it is
----------

Badank is a simple tournament manager.
It can let 2 or more Go/Baduk programs battle (as long as they "speak" GTP).
The result is a pgn-file containing (only) the results, this can then be analyzed using BayesElo or Ordo.
It also outputs an sgf-file that allows you to further analyze the games that have been played.

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



(c) 2021 by Folkert van Heusden <mail@vanheusden.com>
Released under Apache License v2.0
