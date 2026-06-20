/**
 * @file Edict tree-sitter grammar
 * @description Grammar for the Edict concatenative language used by AgentC.
 *
 * Edict is a stack-based, concatenative language with:
 * - Whitespace-delimited terms
 * - Bracket-delimited literals: [code], (code)
 * - JSON objects: {"key": value, ...}
 * - JSON arrays: [value, ...] (in JSON mode)
 * - Operators: @ ! / ^ & | = ==
 * - Context operators: { } < > ( )
 * - Sigil-prefixed names: @name /name ^name
 * - Quote-word: 'word
 * - Comments: #...
 * - Identifiers (dotted, hyphenated, UTF-8)
 */

module.exports = grammar({
  name: 'edict',

  extras: $ => [
    /\s/,
    $.comment,
  ],

  rules: {
    source: $ => repeat($._term),

    _term: $ => choice(
      $.literal,
      $.string,
      $.quote_word,
      $.identifier,
      $.number,
      $.operator,
      $.sigil_expression,
      $.context_block,
      $.json_object,
    ),

    // Bracket-delimited literal: [ ... ] or ( ... )
    // These are self-delimiting code blocks / thunks
    literal: $ => choice(
      seq('[', repeat($._term), ']'),
      seq('(', repeat($._term), ')'),
    ),

    // Double-quoted string with escape support
    string: $ => seq(
      '"',
      repeat(choice(
        $.string_escape,
        /[^"\\]/,
      )),
      '"',
    ),

    string_escape: $ => /\\./,

    // Single-quote prefixed atom: 'word
    quote_word: $ => seq("'", $.identifier),

    // Identifier: dotted names, hyphenated, UTF-8, may start with * - .
    identifier: $ => /[^\s\t\n\r#@\[\](){}<>"'!/^&|,=]+/,

    // Numeric literal (standalone, not part of identifier — identifiers
    // can also be purely numeric strings in Edict, but we tag pure numbers
    // for AST querying convenience)
    number: $ => /\d+/,

    // Operators (standalone, not followed by an identifier)
    operator: $ => choice(
      '!',
      '&',
      '|',
      '=',
      '==',
      // @ / ^ as standalone operators when NOT followed by an identifier
      // (when followed by an identifier, sigil_expression takes precedence)
      token(prec(-1, '@')),
      token(prec(-1, '/')),
      token(prec(-1, '^')),
    ),

    // Sigil expression: @name, /name, ^name (assignment/remove/splice shorthand)
    // The sigil and identifier must be immediately adjacent (no space).
    // Higher precedence than bare operator to resolve @ ambiguity.
    sigil_expression: $ => prec(1, seq(
      token.immediate(choice('@', '/', '^')),
      field('target', $.identifier),
    )),

    // Context block: { ... } or < ... >
    // Lower precedence than json_object so { "key": ... } is parsed as JSON
    context_block: $ => prec(-1, choice(
      seq('{', repeat($._term), '}'),
      seq('<', repeat($._term), '>'),
    )),

    // JSON object: {"key": value, ...}
    json_object: $ => seq(
      '{',
      optional(seq(
        $.json_pair,
        repeat(seq(',', $.json_pair)),
        optional(','),
      )),
      '}',
    ),

    json_pair: $ => seq(
      field('key', choice($.string, $.identifier)),
      ':',
      field('value', $._term),
    ),

    comment: $ => token(seq('#', /.*/)),
  },
});
