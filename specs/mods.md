# Mods

- "Manage Mods" on the strip under the game opens a panel, titled "Mods". It says what a
  mod is (a .zip with a folder whose name ends in .rte, as mod.io gives them, with a link
  to https://mod.io/g/cccp), lists the installed mods with "Remove" beside each, and
  offers "Install a mod…" and "Close".
- "Install a mod…" takes a .zip. One without a Name.rte folder holding an Index.ini at
  its top is refused, and the panel says why. A mod replaces an installed one of the
  same name.
- Mods can be installed and removed before the game starts and while it runs. A change
  applies when the game next starts; while it runs, the panel then offers "Restart the
  game".
- A mod made for another version of the game loads without a dialog. The warning goes to
  the browser's console only, not the game's, and the panel shows it in grey under the
  mod. (Mods on mod.io are made for v6, the released game; this port is built from the
  development branch, v7.)
- The game's own Mod Manager turns installed mods on and off, as upstream's does.
