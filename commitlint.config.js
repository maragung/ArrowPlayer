// Conventional-commit enforcement — REQ-BLD-031.
//
// Two rules here are project-specific rather than conventional-commits defaults:
//
//   * `scope-enum` is the requirement-area table from §0.2, lowercased. A commit
//     whose scope is not an area of the specification is a commit nobody can
//     trace back to a requirement.
//   * `body-references-requirement` is a custom rule: a commit that changes
//     behaviour must name the REQ id it implements, because §1.3 rule 10 makes
//     that part of the Definition of Done. Enforcing it in CI is the difference
//     between a rule and a wish.

/** Requirement areas from §0.2, plus `deps` for dependency bumps. */
const AREAS = [
  'gen', // General / cross-cutting
  'aud', // Audio engine
  'lib', // Library & metadata
  'pls', // Playlists & queue
  'efs', // Format strings
  'net', // Network features
  'syn', // Sync
  'set', // Settings & privacy
  'tst', // Testing
  'thm', // Theme & skin engine
  'uix', // UI/UX
  'key', // Shortcuts & hotkeys
  'osi', // OS integration
  'aut', // Android Auto
  'plg', // Plugin SDK
  'sec', // Security
  'nfr', // Non-functional
  'bld', // Build & release
  'spec', // shared-spec/ contract files
  'deps', // dependency version bumps (dependabot)
];

/**
 * Types that change observable behaviour. A `docs:` or `chore:` commit is
 * exempt from the requirement-id rule; a `feat:` or `fix:` is not.
 */
const BEHAVIOURAL_TYPES = ['feat', 'fix', 'perf', 'revert'];

const REQUIREMENT_ID = /\bREQ-(?:GEN|AUD|LIB|PLS|EFS|NET|SYN|SET|TST|THM|UIX|KEY|OSI|AUT|PLG|SEC|NFR|BLD)-\d{3}\b/;

module.exports = {
  extends: ['@commitlint/config-conventional'],

  plugins: [
    {
      rules: {
        /**
         * REQ-BLD-031 / §1.3 rule 10: every commit touching behaviour must
         * reference its requirement ID in the body.
         *
         * A revert is allowed to inherit the id from the commit it reverts, so
         * the check accepts a `Reverts <sha>` trailer in place of an id.
         */
        'body-references-requirement': ({ type, body, footer }) => {
          if (!BEHAVIOURAL_TYPES.includes(type)) return [true];

          const text = `${body || ''}\n${footer || ''}`;
          if (REQUIREMENT_ID.test(text)) return [true];
          if (type === 'revert' && /\bReverts\b/i.test(text)) return [true];

          return [
            false,
            `a "${type}" commit must reference its requirement ID in the body ` +
              '(e.g. "Refs: REQ-AUD-035") — see REQ-BLD-031 and §1.3 rule 10',
          ];
        },
      },
    },
  ],

  rules: {
    'type-enum': [
      2,
      'always',
      ['feat', 'fix', 'perf', 'refactor', 'docs', 'test', 'build', 'ci', 'chore', 'revert'],
    ],
    'scope-enum': [2, 'always', AREAS],
    'scope-case': [2, 'always', 'lower-case'],

    // A scope is not mandatory — a genuinely cross-cutting chore has none — but
    // when present it must be an area of the specification.
    'scope-empty': [0],

    'subject-case': [2, 'never', ['pascal-case', 'upper-case']],
    'subject-empty': [2, 'never'],
    'subject-full-stop': [2, 'never', '.'],

    // 72 so `git log --oneline` stays readable in an 80-column terminal.
    'header-max-length': [2, 'always', 72],

    // Bodies carry the reasoning and the REQ ids; wrapping is enforced so they
    // stay readable in `git log`, but the limit is generous for tables.
    'body-max-line-length': [2, 'always', 100],
    'footer-max-line-length': [2, 'always', 100],
    'body-leading-blank': [2, 'always'],
    'footer-leading-blank': [2, 'always'],

    'body-references-requirement': [2, 'always'],
  },
};
