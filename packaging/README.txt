LSFG-NX development package

Nothing in this package generates frames yet. The plugin observes the game's
presentation path and forwards every call unchanged.

Do not place Lossless.dll in a distributed package. Each user must supply their
own legally obtained file at:

  `/switch/lsfg-nx/Lossless.dll`

The plugin only loads in these games:

  `/SaltySD/plugins/0100ECD018EBE000/`  Paper Mario: The Thousand-Year Door
  `/SaltySD/plugins/0100E95004038000/`  Xenoblade Chronicles 2

It reads its title record from:

  `/SaltySD/plugins/lsfg-nx/profiles/<title-id>/profile.ini`
