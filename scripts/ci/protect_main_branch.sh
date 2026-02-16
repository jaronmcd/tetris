#!/usr/bin/env bash
set -euo pipefail

BRANCH="main"
REPO="${1:-}"
REQUIRED_CHECK_CONTEXT="${2:-Web Flasher (ESP32) / build}"

if ! command -v gh >/dev/null 2>&1; then
  echo "Error: GitHub CLI (gh) is required." >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "Error: jq is required." >&2
  exit 1
fi

if [[ -z "${REPO}" ]]; then
  REPO="$(gh repo view --json nameWithOwner --jq '.nameWithOwner')"
fi

if [[ -z "${REPO}" ]]; then
  echo "Error: Could not determine repository. Pass it as owner/repo." >&2
  exit 1
fi

if ! gh auth status >/dev/null 2>&1; then
  echo "Error: gh is not authenticated. Run: gh auth login" >&2
  exit 1
fi

payload="$(
  jq -n --arg check "${REQUIRED_CHECK_CONTEXT}" '
  {
    required_status_checks: {
      strict: true,
      contexts: [$check]
    },
    enforce_admins: true,
    required_pull_request_reviews: {
      dismiss_stale_reviews: true,
      require_code_owner_reviews: false,
      required_approving_review_count: 1
    },
    restrictions: null,
    required_linear_history: false,
    allow_force_pushes: false,
    allow_deletions: false,
    block_creations: false,
    required_conversation_resolution: true,
    lock_branch: false,
    allow_fork_syncing: true
  }'
)"

gh api \
  --method PUT \
  -H "Accept: application/vnd.github+json" \
  "/repos/${REPO}/branches/${BRANCH}/protection" \
  --input - <<<"${payload}" >/dev/null

echo "Branch protection configured:"
echo "  repo: ${REPO}"
echo "  branch: ${BRANCH}"
echo "  required check: ${REQUIRED_CHECK_CONTEXT}"
