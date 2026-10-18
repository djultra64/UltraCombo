Put your own Killer Instinct 2 arcade files (MAME's "kinst2" set) in this
folder:

  ki2-l14.u98                    boot ROM (version L1.4)
  kinst2.chd                     hard disk, MAME's CHD as it is
  ki2_l1.u10 ki2_l1.u11 ki2_l1.u12 ki2_l1.u13
  ki2_l1.u33 ki2_l1.u34 ki2_l1.u35 ki2_l1.u36   sound ROMs

The ROM files are the ones in MAME's kinst2.zip: unzip them here. The CHD
can also stay in a "kinst2" folder inside this one, as MAME keeps it. A raw
disk image (kinst2.raw, from "chdman extractraw") works as well.

The disk is only read. The game's settings and high scores are kept in the
"saves" folder beside the program.

To check you have the right files, compare their MD5 or SHA1 checksums with
the table in README.md, section "Checking your files". The program also
warns at startup if a file doesn't match.
