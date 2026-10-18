Put your own arcade game files in this folder: either game, or both.
These are the files of MAME's sets, as they come.

Killer Instinct (MAME's "kinst" set):
  ki-l15d.u98                    boot ROM, version L1.5D
  ki-l14.u98                     boot ROM, version L1.4 (optional)
  u10-l1 u11-l1 u12-l1 u13-l1
  u33-l1 u34-l1 u35-l1 u36-l1    sound ROMs
  kinst.chd                      hard disk, MAME's CHD as it is

Killer Instinct 2 (MAME's "kinst2" set):
  ki2-l14.u98                    boot ROM, version L1.4
  ki2_l1.u10 ki2_l1.u11 ki2_l1.u12 ki2_l1.u13
  ki2_l1.u33 ki2_l1.u34 ki2_l1.u35 ki2_l1.u36   sound ROMs
  kinst2.chd                     hard disk, MAME's CHD as it is

Unzip the ROMs here. A CHD can also stay in a folder named after its set
("kinst/kinst.chd", "kinst2/kinst2.chd"), the way MAME keeps them. Raw
disk images (kinst.raw, kinst2.raw, from "chdman extractraw") work as well.
The file names of the two games don't clash.

The disks are only read. The games' settings and high scores are kept in
the "saves" folder beside the program.

To check you have the right files, compare their MD5 or SHA1 checksums with
the tables in README.md, section "Checking your files". The program also
warns at startup if a file doesn't match.
