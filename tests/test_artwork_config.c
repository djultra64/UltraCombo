#include "config.h"
#include "game.h"
#include <assert.h>
#include <stdio.h>
int main(int argc,char **argv)
{
    assert(argc==2);
    game_select(&game_ki1);
    FILE *file=fopen(argv[1],"w"); assert(file);
    fputs("[video]\nscale=integer\n",file); assert(fclose(file)==0);
    config_load(argv[1]);
    assert(cfg.artwork && cfg.scale_mode==SCALE_INTEGER);
    cfg.artwork=false; assert(config_save());
    config_load(argv[1]); assert(!cfg.artwork && cfg.scale_mode==SCALE_INTEGER);
    cfg.artwork=true; assert(config_save());
    config_load(argv[1]); assert(cfg.artwork);
    assert(remove(argv[1])==0);
    puts("artwork configuration round-trip: PASS");
}
