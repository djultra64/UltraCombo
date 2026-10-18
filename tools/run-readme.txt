Put your own Killer Instinct 2 arcade files in this folder:

  ki2-l14.u98                    boot ROM (version L1.4)
  kinst2.raw                     hard disk image
  ki2_l1.u10 ki2_l1.u11 ki2_l1.u12 ki2_l1.u13
  ki2_l1.u33 ki2_l1.u34 ki2_l1.u35 ki2_l1.u36   sound ROMs

kinst2.raw is extracted from MAME's kinst2.chd with MAME's chdman tool:

  chdman extractraw -i kinst2.chd -o kinst2.raw

The ROM files are the ones in MAME's kinst2 set (unzip it here).
The game saves its settings and high scores into kinst2.raw.

To check you have the right files, compare their MD5 or SHA1 checksums with
the table in README.md, section "Checking your files".
