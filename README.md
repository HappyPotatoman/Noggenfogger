A noggenfogger version of cfish.
Notable changes:
- 12 piece counterMovesHistory table and PieceToHistory sizes saving entries as int8 and combining inCheck || capture_or_promotes to save more space,
- 2048 pawn entries, since 1024 was preforming much worse and 4096 was not much better, but took a lot of memory,
- half material table size,
- correction histories (pawn, minor and non-pawn),
- mallocs were moved to extern tables (no idea if this helped or not)
- a lot of SF commits from where the master branch of cfish left off as well as some commits that improved time sudden death time control.
