# TrialChamberFinder
Finds the spots with the most trial spawners, vaults or ominous vaults inside an area on a set seed.

//replacenear 100 !trial_spawner,vault air

Search time grows with the area searched: doubling the search radius takes about 4 times as long. 
Any ideas/issues, DM me xvyz#4060

# Versions
| Pick | For |
|---|---|
| 1.20.3 (experimental) | 1.20.3 - 1.20.4 |
| 1.20.5 (experimental) | 1.20.5 - 1.20.6 |
| 1.21 | 1.21 - 1.21.1 |
| 1.21.2 | 1.21.2 - 26.2 |

Pick the closest version at or below yours. The experimental versions are for worlds with the Update 1.21
experiment. Trial chambers changed in 1.20.5 (new rooms, vaults), 1.21 (new rooms, slime and spider swapped mob
groups) and 1.21.2 (beds and other new pieces), and have not changed since. In the experimental versions chambers
also generate in deep dark and their pieces are not limited to the world height, and 1.20.3 - 1.20.4 place chambers
on a different grid.

# Input parameters
**Version** - see above  
**Seed** - the world seed in numeric form  
**What to count** - `TRIAL_SPAWNERS`, `VAULTS` (normal and ominous) or `OMINOUS_VAULTS`  
**Spawner mobs to leave out** - only asked when counting spawners, for example leave out slime spawners  
**Area type** - `CHAMBERS`, `SQUARE`, `RECTANGLE` or `CIRCLE`, sizes in blocks
- chambers: number of chambers (1-4) instead of a size; finds the chamber, or the group of neighbouring chambers,
  with the most in total. A group is a chamber and its nearest chambers.
- square: radius r, so the square is 2r+1 blocks wide
- rectangle: width (X) and length (Z); both orientations are tried and the result shows which one won
- circle: radius

**Minimum count to report** - 1 to 200; leave empty for 25 when counting spawners, 1 for vaults and ominous vaults  
**Number of results to show** - leave empty for 10  
**Sort results by** - only asked for `CHAMBERS` with 2-4 chambers
- `AMOUNT` - highest total first (the default)
- `CLOSENESS` - the groups whose chambers are closest together, whatever their total
- `AMOUNT_THEN_CLOSENESS` - the same groups `AMOUNT` finds, listed closest together first

**Search radius** - in blocks; every chamber whose start is within this distance of the center is included. Any
radius up to the world border works; memory does not grow with it  
**Search center** - block position of the search center (leave empty for 0 0)  
**Number of threads** - leave empty to use every thread the computer has; the results are the same for any number

An invalid value is simply asked for again. Inputs can also be piped in, one per line, in the order above.

A search shows its progress as it runs. Areas thousands of blocks across take much longer
than farm-sized ones, most of all when counting spawners.

# Output
```
Found 83 trial spawners (4 chambers) in square r=300 (2423 -919 to 3023 -319) at /tp 2723 ~ -619
    spawners 83 (9 zombie, 3 husk, 8 cave_spider, 3 baby_zombie, 28 skeleton, 23 stray, 9 breeze)  vaults 83 (ominous 28)
```
- `/tp` goes to the center of the area; squares and rectangles also show their corners
- the count is exact for that area; "(4 chambers)" is how many chambers the counted spawners or vaults come from
- the second line lists every spawner by mob and every vault inside the same area; mobs you left out are still
  listed, marked `[left out]`, but not counted
- chambers the game does not place (deep dark) are never counted
- results with the same set of chambers are only shown once

With `CHAMBERS` every chamber of the group is listed with its own count:
```
Found 76 trial spawners in 3 chambers, up to 963 blocks apart
    /tp 2729 -31 -3079  (28 trial spawners)
    /tp 3017 -31 -2649  (24 trial spawners)
    /tp 2919 -31 -3607  (24 trial spawners)
    spawners 76 (13 husk, 6 spider, 4 cave_spider, 4 baby_zombie, 23 skeleton, 17 stray, 9 breeze)  vaults 77 (ominous 25)
```

Every chamber picks one ranged mob (skeleton, stray or bogged), one melee mob and one small mob for all of its
spawners of that kind; breeze spawners are always breeze.

# Building
Windows: download `TrialChamberFinder.exe` from Releases, or run `build.bat` from a Visual Studio Developer Command Prompt.

Linux and macOS (gcc or clang, any 32-bit or 64-bit CPU including Apple Silicon):
```
gcc -o TrialChamberFinder -std=gnu99 -O2 -ffp-contract=off ./src/*.c ./src/*/*.c -lm -lpthread
```
`compile.txt` has this and a MinGW command. Keep `-ffp-contract=off`: biome noise must not use fused multiply-add
to match the game. Run `./TrialChamberFinder selftest` once on a new system; it checks the fast random number code
against the plain one and prints `0 failures`.

Memory: about 25 MB, plus the part of the world being searched, at most about 250 MB. It does not grow with the
search radius: an 11 million chamber search stays at about 250 MB.

# How it works
**Placement.** Trial chambers use a random spread placement (spacing 34 chunks, separation 12; 32 and 8 before 1.20.5).
From 1.21 on, a chamber exists unless the biome at its start is deep dark; biomes come from cubiomes.

**Layout.** A port of the game's jigsaw generator (`JigsawPlacement`): from the start piece, every open connector
tries pieces from its pool in the game's random order until one fits, driven by the same `java.util.Random`
sequence. The pieces, connectors and pools of every version were read from the game files and are in
`src/chambergenerator/ChamberData.h`.

**Speed.** About 8,000 chambers per second per thread, and the whole search uses every thread. A search radius of
1,000,000 blocks (about 10 million chambers) takes about 3 minutes on a 16-thread CPU. The game spends most of
its time trying pieces that cannot fit; only the random numbers those tries use up matter, so they are skipped with
exact jumps in the random sequence instead of being redone.

**Tiles.** A search runs over one part of the world at a time, each with a margin wide enough that areas crossing
the edge still see every chamber they need, keeping only the best results. That is what keeps memory flat at any
search radius.

**Best area.** For every chamber, the best placement of the area is searched among all spawners or vaults that
could share an area with it, using exact integer math. For `CHAMBERS`, every chamber is grouped with its nearest
chambers (by start position) and the groups are ranked by their total, or by how far apart the group's chambers
are, depending on the sort chosen.

# Credits
Biome generation: [cubiomes](https://github.com/Cubitect/cubiomes) by Cubitect (MIT license, see
`src/cubiomes/LICENSE`); only the parts needed for 1.20+ overworld biomes are included.  
Trial chamber pieces, connectors and pools: Minecraft, by Mojang.  
Modeled on [CrossroadFinder](https://github.com/Gaider10/CrossroadFinder) by Gaider10.
