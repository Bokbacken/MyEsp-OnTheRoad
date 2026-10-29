# Multimap and portal test

Copy this folder to `/otr/adventure/multimap_portals/` on the SD card. Start
**The hidden map** from the adventure menu.

1. Walk right from `S`. The stone pickup shares a portal tile: it is collected
   once, then the player arrives in the cave.
2. Save while standing on the cave landing tile and continue. The arrival
   portal must not immediately send the player back.
3. Walk down to collect the second `stone` resource. The inventory is shared
   across maps and is capped at two.
4. Push the rock left onto the cave landing at `(2,1)`. Walk through the
   lower corridor to the portal at `(7,1)` to return home.
5. Try the first portal again. Its cave arrival is now blocked by the rock;
   the trip must be rejected without moving or deleting it. Use the second
   home portal at `(7,3)` to enter the cave through its side landing, then
   push the rock right off `(2,1)` to open the main arrival.
6. Step on the cave checkpoint before returning home, then enter the lava
   tile on the home map to lose a life. Respawn must return to the cave
   checkpoint.

Group 1 exists only on the main map; timed group 2 and its own plate/gate exist
only in the cave. The cave also has its own NPC, chaser, fog, and dark region.
The scout's `world travel cave` effect uses the same named destination table as
portals. Restart still restarts the whole adventure.

The host regression test exercises the same world transitions and save image.
