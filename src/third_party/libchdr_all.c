// libchdr as one compilation unit: its own unity.c, plus the A/V codec that
// unity.c leaves out but the reader's codec table refers to.
#include "libchdr/unity.c"
#include "libchdr/src/libchdr_codec_avhuff.c"
