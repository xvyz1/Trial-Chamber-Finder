# TrialChamberFinder
Finds trial chambers with the most spawners, vaults or ominous vaults in an area on Minecraft Java. Vibecoded.

//replacenear 100 !trial_spawner,vault air

Search time grows with the area searched: doubling the search radius takes about 4 times as long. Any ideas/issues,
DM me xvyz#4060

# Run
Run `TrialChamberFinder.exe` (Windows), or build it from the source for Linux and macOS (see Build). One file, nothing to install.

# Versions
| Pick | For |
|---|---|
| 1.20.3 (experimental) | 1.20.3 - 1.20.4 |
| 1.20.5 (experimental) | 1.20.5 - 1.20.6 |
| 1.21 | 1.21 - 1.21.1 |
| 1.21.2 | 1.21.2 - 26.3 |

Pick the closest version at or below yours. The experimental versions are for worlds with the Update 1.21
experiment. Trial chambers changed in 1.20.5 (new rooms, vaults), 1.21 (new rooms, slime and spider swapped mob
groups) and 1.21.2 (beds and other new pieces), and have not changed since. In the experimental versions chambers
also generate in deep dark and their pieces are not limited to the world height, and 1.20.3 - 1.20.4 place chambers
on a different grid.

# Input parameters
**Version** - see above  
**Seed** - the world seed in numeric form  
**What to count** - `1` TRIAL_SPAWNERS, `2` VAULTS (normal and ominous) or `3` OMINOUS_VAULTS  
**Spawner mobs to leave out** - only asked when counting spawners: numbers 1-11 separated by commas without spaces, for example `4,5` leaves out slime and cave spider spawners  
**Area type** - `1` # OF CHAMBERS, `2` SQUARE or `3` RECTANGLE, sizes in blocks
  with the most in total. A group is a chamber and its nearest chambers.
- square: radius r, so the square is 2r+1 blocks wide
- rectangle: width (X) and length (Z); both orientations are tried and the result shows which one won

**Minimum count of trial spawners / vaults / ominous vaults to report** (the prompt names what you chose) - 1 to 200;
leave empty for 20 per chamber when counting spawners (20 for a size area or 1 chamber, 40 for 2 chambers, 60 for 3,
80 for 4), 1 for vaults and ominous vaults  
**Number of results to show** - leave empty for 20  
**Sort results by** - only asked for `# OF CHAMBERS` with 2-4 chambers
- `1` AMOUNT_THEN_CLOSENESS - the groups with the highest total, listed closest together first
- `2` AMOUNT - highest total first
- `3` CLOSENESS - the groups whose chambers are closest together, whatever their total

**Search radius** - in blocks; every chamber whose start is within this distance of the center is included. Any
radius up to the world border works; memory does not grow with it  
**Search center** - block position of the search center (leave empty for 0 0)  
**Number of threads** - leave empty to use every thread the computer has (the program counts them itself and honours a restricted CPU affinity); the results are the same for any number  
**CSV output** - `1` YES or `2` NO (leave empty for NO). The file is named after the seed and what was counted, for example `777_TRIAL_SPAWNERS.csv` (a number is added if that file already exists), and is written next to the program with the shown results: rank, count, chambers, `/tp`, the area corners, spawners, vaults, ominous vaults and the spawner types written as `3 zombie 4 cave_spider` (only the mobs that are there). Results of the `# OF CHAMBERS` area type also list each chamber's `/tp` and spawner count.

An invalid value is simply asked for again. Inputs can also be piped in, one per line, in the order above.

While a search runs it first says `Initiating...`; after the first 10 seconds it prints the percentage done with the time elapsed and an estimate of the time left. A search that
will take under a minute in total prints every 10%, one that takes 1 to 5 minutes every 5%, a longer one every 1%. The percentage is the share of the work done; processors boost higher in the first seconds, so it can run
slightly ahead of the clock at the start. Areas thousands of blocks across take much longer than farm-sized ones, most of all when counting spawners.

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

With `# OF CHAMBERS` every chamber of the group is listed with its own count:
```
Found 76 trial spawners in 3 chambers, up to 963 blocks apart
    /tp 2729 -31 -3079  (28 trial spawners)
    /tp 3017 -31 -2649  (24 trial spawners)
    /tp 2919 -31 -3607  (24 trial spawners)
    spawners 76 (13 husk, 6 spider, 4 cave_spider, 4 baby_zombie, 23 skeleton, 17 stray, 9 breeze)  vaults 77 (ominous 25)
```

Every chamber picks one ranged mob (skeleton, stray or bogged), one melee mob and one small mob for all of its
spawners of that kind; breeze spawners are always breeze.

# Large Biomes and other world types
**Is it the same for Large Biomes worlds?** Almost. Where a trial chamber starts and what it looks like (rooms, spawners, vaults) do not depend on the world type; only the check that a chamber does not
generate in a deep dark biome does, and the finder always uses the normal biome size. In a Large Biomes world the deep dark patches are in other places: on 3 seeds checked, a different verdict for about
6-8% of the chambers (about 4% of all chambers are skipped as deep dark in either world type, but not the same ones). So for a Large Biomes world the results can include a chamber the game does not place
or leave out one it does; the counts of the chambers that are shown are right. Large Biomes is not an option in the program yet; other world types (Amplified, Single Biome) are not covered either.

# How it works
**Placement.** Trial chambers use a random spread placement (spacing 34 chunks, separation 12; 32 and 8 before 1.20.5).
From 1.21 on, a chamber exists unless the biome at its start is deep dark; biomes come from cubiomes.

**Layout.** A port of the game's jigsaw generator (`JigsawPlacement`): from the start piece, every open connector
tries pieces from its pool in the game's random order until one fits, driven by the same `java.util.Random`
sequence. The pieces, connectors and pools of every version were read from the game files and are in
`src/chambergenerator/ChamberData.h`.

**Tiles.** A search runs over one part of the world at a time, each with a margin wide enough that areas crossing
the edge still see every chamber they need, keeping only the best results. That is what keeps memory flat at any
search radius.

**Best area.** For every chamber, the best placement of the area is searched among all spawners or vaults that
could share an area with it, using exact integer math. For `CHAMBERS`, every chamber is grouped with its nearest
chambers (by start position) and the groups are ranked by their total, or by how far apart the group's chambers
are, depending on the sort chosen.

# Speed
About 8,000 chambers per second per thread, and the whole search uses every thread. A search radius of
1,000,000 blocks (about 10 million chambers) takes about 3 minutes on a 16-thread CPU. The game spends most of
its time trying pieces that cannot fit; only the random numbers those tries use up matter, so they are skipped with
exact jumps in the random sequence instead of being redone.

# Build
Windows: run `build.bat` from a Visual Studio Developer Command Prompt (or download `TrialChamberFinder.exe` from Releases).

Linux and macOS (gcc or clang, any 32-bit or 64-bit CPU including Apple Silicon):
```
gcc -o TrialChamberFinder -std=gnu11 -O2 -ffp-contract=off ./src/*.c ./src/*/*.c -lm -lpthread
```
`compile.txt` has this and a MinGW command. Keep `-ffp-contract=off`: biome noise must not use fused multiply-add
to match the game. Run `./TrialChamberFinder selftest` once on a new system; it checks the fast random number code
against the plain one and prints `0 failures`.

Memory: about 25 MB, plus the part of the world being searched, at most about 250 MB. It does not grow with the
search radius: an 11 million chamber search stays at about 250 MB.

# Credits
Biome generation: [Cubiomes](https://github.com/Cubitect/cubiomes) by Cubitect (MIT license, see
`src/cubiomes/LICENSE`); only the parts needed for 1.20+ overworld biomes are included.  
Modeled on [CrossroadFinder](https://github.com/Gaider10/CrossroadFinder) by Gaider10.
