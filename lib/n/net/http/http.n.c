#ifdef NLANG_DEFINE_FUNCTIONS

static n$builtins$Bool n$net$http$is_token(n$builtins$U8 r) {
  static n$builtins$Bool lut[127] = {
    ['!'] = 1,
    ['#'] = 1,
    ['$'] = 1,
    ['%'] = 1,
    ['&'] = 1,
    ['\''] = 1,
    ['*'] = 1,
    ['+'] = 1,
    ['-'] = 1,
    ['.'] = 1,
    ['0'] = 1,
    ['1'] = 1,
    ['2'] = 1,
    ['3'] = 1,
    ['4'] = 1,
    ['5'] = 1,
    ['6'] = 1,
    ['7'] = 1,
    ['8'] = 1,
    ['9'] = 1,
    ['A'] = 1,
    ['B'] = 1,
    ['C'] = 1,
    ['D'] = 1,
    ['E'] = 1,
    ['F'] = 1,
    ['G'] = 1,
    ['H'] = 1,
    ['I'] = 1,
    ['J'] = 1,
    ['K'] = 1,
    ['L'] = 1,
    ['M'] = 1,
    ['N'] = 1,
    ['O'] = 1,
    ['P'] = 1,
    ['Q'] = 1,
    ['R'] = 1,
    ['S'] = 1,
    ['T'] = 1,
    ['U'] = 1,
    ['W'] = 1,
    ['V'] = 1,
    ['X'] = 1,
    ['Y'] = 1,
    ['Z'] = 1,
    ['^'] = 1,
    ['_'] = 1,
    ['`'] = 1,
    ['a'] = 1,
    ['b'] = 1,
    ['c'] = 1,
    ['d'] = 1,
    ['e'] = 1,
    ['f'] = 1,
    ['g'] = 1,
    ['h'] = 1,
    ['i'] = 1,
    ['j'] = 1,
    ['k'] = 1,
    ['l'] = 1,
    ['m'] = 1,
    ['n'] = 1,
    ['o'] = 1,
    ['p'] = 1,
    ['q'] = 1,
    ['r'] = 1,
    ['s'] = 1,
    ['t'] = 1,
    ['u'] = 1,
    ['v'] = 1,
    ['w'] = 1,
    ['x'] = 1,
    ['y'] = 1,
    ['z'] = 1,
    ['|'] = 1,
    ['~'] = 1,
  };
  return r < sizeof(lut)/sizeof(lut[0]) && lut[r];
}

#endif
