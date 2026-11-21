# Apps Script Dashboard comparison

- `Dashboard.user-supplied.html` is an exact byte copy of the HTML provided by the user on 2026-11-21 after rolling back the deployment to version 45. The Google deployment version identity has not been independently verified.
- `Dashboard.latest-2e6397d.html` is from local branch `codex/edge-ai-integration` commit `2e6397d`.
- `Dashboard.diff.patch` compares those two files. No Apps Script deployment was changed.
- User-supplied SHA-256: `d73321ded93a853d019bb75d41589fe19997b814aa6f07c97c62062380be34a4`
- Latest source SHA-256: `5f9878469e8e5f5921f7f3202a6d113b793d240906eb5d0c7012862fafc83803`
- `Dashboard.newer-user-supplied.html` is an exact byte copy of the second HTML attached by the user; SHA-256 `a8c87464d34028a876c356430f385ab4d6644c903a4d98b6857521a91caa4a97`. It includes the new IP UI and render error catch.
- `Dashboard.user-supplied-comparison.patch` compares the two user-provided HTML files.
