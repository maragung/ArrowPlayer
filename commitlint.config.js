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

/**
 * Requirement areas from §0.2, plus the two scopes Dependabot writes.
 *
 * `deps` and `deps-dev` are not a stylistic choice: dependabot-core picks
 * between them with `dependencies.any?(&:production?) ? "deps" : "deps-dev"`,
 * and there is no setting that changes it. Every Node dependency here is a
 * devDependency, so `deps-dev` is what its commits will actually carry — and a
 * scope-enum that omitted it would turn every Dependabot pull request red for a
 * reason nobody could act on.
 */
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
  'deps', // production dependency bumps (Dependabot, or by hand)
  'deps-dev', // development dependency bumps (Dependabot's own scope)
];

/**
 * Dependabot's commit subjects, in the two grammars it emits — `bump <name>
 * from <a> to <b>` (optionally `in the <group> group`) and `bump the <group>
 * group with <n> updates`. Used to relax the header limit for those and nothing
 * else; see `header-max-length-dependency-aware` below.
 */
const DEPENDENCY_BUMP_SUBJECT =
  /^bump (?:the .+ group\b.*|\S.* from \S+ to \S+(?: in the .+ group)?)$/;

/** The normal limit, and the bounded exception for the subjects above. */
const HEADER_LIMIT = 72;
const HEADER_LIMIT_BUMP = 100;

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
        /**
         * 72 columns, except for a dependency bump, which cannot be shortened.
         *
         * The 72 below exists so `git log --oneline` stays readable in an
         * 80-column terminal, and that reason still holds. But Dependabot's
         * subject is generated from names it does not choose: measured against
         * this repository's own dependencies, `ci(deps-dev): bump
         * markdownlint-cli2 from 0.23.2 to 0.24.0 in the node-tooling group` is
         * 84 characters, and `@commitlint/config-conventional` alone reaches 72
         * with no group suffix at all.
         *
         * Three ways out, and why this one. Raising the limit for everything
         * gives up a rule that is doing real work. Adding the bot to
         * `ignores` exempts a whole class of commit from every rule, not just
         * this one. So the exception is scoped to the message shape rather than
         * the author: a subject in Dependabot's own bump grammar gets 100
         * columns — still bounded, so a pathological header fails — and keeps
         * every other rule, including scope-enum and the body rules. A human
         * who writes that exact shape gets the same allowance, which is
         * harmless, because the shape *is* a dependency bump.
         */
        'header-max-length-dependency-aware': ({ header, subject }) => {
          const limit = DEPENDENCY_BUMP_SUBJECT.test(subject || '')
            ? HEADER_LIMIT_BUMP
            : HEADER_LIMIT;
          const length = (header || '').length;
          if (length <= limit) return [true];

          const why =
            limit === HEADER_LIMIT_BUMP
              ? ' — the dependency-bump allowance, already the wider of the two'
              : '';
          return [
            false,
            `header must not be longer than ${limit} characters${why}, ` +
              `current length is ${length}`,
          ];
        },

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

    // Off, and replaced by `header-max-length-dependency-aware` below: same 72
    // columns, with a bounded exception for the one subject shape that cannot
    // be shortened. Leaving both on would mean the built-in fires first and the
    // exception never applies.
    'header-max-length': [0],

    // Bodies carry the reasoning and the REQ ids; wrapping is enforced so they
    // stay readable in `git log`, but the limit is generous for tables.
    'body-max-line-length': [2, 'always', 100],
    'footer-max-line-length': [2, 'always', 100],
    'body-leading-blank': [2, 'always'],
    'footer-leading-blank': [2, 'always'],

    'header-max-length-dependency-aware': [2, 'always'],
    'body-references-requirement': [2, 'always'],
  },
};
