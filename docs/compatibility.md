# Compatibility

| webOS | TV years | Status |
|---|---|---|
| 3.x | 2016 to 2017 | Should work. Includes the start-up fix for these TVs |
| 4.x | 2018 to 2019 | Works. Tested on webOS 4.4 |
| 5.x | 2020 | Works, by reports |
| 6.x | 2021 | Works, by reports |
| 22 | 2022 | Works, by reports |
| 23 and 24 | 2023 to 2024 | Should work, not tested yet |
| 25 | 2025 | Works, by reports, live streams included |

Reports for the code ORIGIN shares with Stremio Origin come from people running either app. If you try ORIGIN on a TV not listed as working, Settings › About › Share logs gives a short code to paste in an issue.

## Install
- Homebrew Channel (rooted TVs): add the repository `https://slvjtr1.github.io/origin/apps.json`, then install Origin.
- Or download `tv.stremioorigin.app_<version>_arm.ipk` from [Releases](https://github.com/slvjtr1/origin/releases/latest).
- Install it with [webOS Dev Manager](https://github.com/webosbrew/dev-manager-desktop) or `ares-install` (Developer Mode or a rooted TV with Homebrew Channel).
- After that, updates install from inside the app: Settings › About.
