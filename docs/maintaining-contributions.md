# Maintaining contributions

The contributor-facing process is in [CONTRIBUTING.md](../CONTRIBUTING.md).
This page records the GitHub controls that support it.

## Main branch protection

The `protect-main` repository ruleset targets the default branch. Its desired
configuration is versioned in [main.json](../.github/rulesets/main.json).
GitHub enforces the live ruleset; committing this JSON does not apply it.

The rules require a pull request, resolved review conversations, and all nine
named GitHub Actions checks. The PR must be up to date with `main`. Check results
must come from the GitHub Actions app (integration ID `15368`), rather than an
arbitrary status publisher. Force pushes and branch deletion remain prohibited,
and no bypass actors are configured.

This is a single-maintainer repository, so the required approving-review count
is **zero**. GitHub does not allow authors to approve their own PRs; requiring
one other approval would block the owner's own maintenance work. The maintainer
still reviews external contributions before merging and records the decision
on the PR. This policy does not claim that GitHub enforces an independent human
review. Revisit the approval count when a second active maintainer joins.

## Applying or updating the rules

The rules live in `.github/rulesets/` (`dev.json`, `main.json`). On a
repository without them yet (this one became standalone on 2026-10-09), create
them once from the repository root with an account that can manage rules:

```sh
gh api --method POST repos/ftornberg/vibepulse/rulesets \
  --input .github/rulesets/dev.json
gh api --method POST repos/ftornberg/vibepulse/rulesets \
  --input .github/rulesets/main.json
```

After reviewing a change to the JSON, update a ruleset in place by its ID
(`gh api repos/ftornberg/vibepulse/rulesets` lists them):

```sh
gh api --method PUT repos/ftornberg/vibepulse/rulesets/<id> \
  --input .github/rulesets/dev.json
```

Inspect the returned configuration to confirm it matches the intended rules.
Update this file if the ruleset is replaced or the repository moves.

When renaming or adding a required CI job, update the workflow and ruleset
together. GitHub matches exact check names, including matrix suffixes. Verify a
real PR run reports each required name before activating a new requirement;
otherwise a missing check can leave every PR waiting indefinitely. Do not remove
a failing check simply to merge the change it caught.

## Handling external contributions

1. Read the complete diff, including workflow changes, before enabling a
   first-time contributor's CI run. The repository requires this approval for
   first-time contributors; it is not a code review approval.
2. Thank the contributor and identify concrete defects or missing evidence.
   Request changes for issues that need fixing before merge; distinguish optional
   suggestions from requirements.
3. For a small integration fix, either ask for an update in the same PR or add a
   commit when maintainer edits are enabled. Explain what was added and preserve
   authorship. Keep unrelated existing defects in separate work.
4. Review the final diff after updates, refresh the branch when `main` changes,
   and wait for required checks. Approve an external PR when it is ready. Squash
   merge it with an accurate title and retained author/co-author credits.

Green CI is automated evidence. Physical validation and release or flashing
authorization still follow the project's existing hardware rules.
