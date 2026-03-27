LOAD 'passwordcheck';

CREATE USER regress_user1;

-- ============================================================
-- Part 1: Default policy (passwordcheck.strict_policy = off)
--   Rules: >= 8 chars, not contain username, both letters and nonletters
-- ============================================================

SHOW passwordcheck.strict_policy;

-- ===== 1A: Password too short (minimum 8 characters) =====

-- error: 1 character
ALTER USER regress_user1 PASSWORD 'a';

-- error: 2 characters
ALTER USER regress_user1 PASSWORD 'a1';

-- error: 3 characters
ALTER USER regress_user1 PASSWORD 'ab1';

-- error: 4 characters
ALTER USER regress_user1 PASSWORD 'ab1!';

-- error: 5 characters
ALTER USER regress_user1 PASSWORD 'abc1!';

-- error: 6 characters
ALTER USER regress_user1 PASSWORD 'abc12!';

-- error: 7 characters
ALTER USER regress_user1 PASSWORD 'abc123!';

-- ok: exactly 8 characters (boundary)
ALTER USER regress_user1 PASSWORD 'abcd123!';

-- ok: 9 characters
ALTER USER regress_user1 PASSWORD 'abcde123!';

-- ===== 1B: Password contains user name =====

-- error: username at start
ALTER USER regress_user1 PASSWORD 'regress_user1abc123!';

-- error: username in middle
ALTER USER regress_user1 PASSWORD 'xxregress_user1yy1!';

-- error: username at end
ALTER USER regress_user1 PASSWORD 'abc1!regress_user1';

-- ok: uppercase username (case-sensitive, no match)
ALTER USER regress_user1 PASSWORD 'REGRESS_USER1abc1!';

-- ===== 1C: Must contain both letters and nonletters =====

-- error: only letters
ALTER USER regress_user1 PASSWORD 'abcdefghij';

-- error: only letters (uppercase)
ALTER USER regress_user1 PASSWORD 'ABCDEFGHIJ';

-- error: only letters (mixed case)
ALTER USER regress_user1 PASSWORD 'AbCdEfGhIj';

-- error: only nonletters (digits)
ALTER USER regress_user1 PASSWORD '1234567890';

-- error: only nonletters (specials)
ALTER USER regress_user1 PASSWORD '!@#$%^&*()';

-- error: only nonletters (mixed digits and specials)
ALTER USER regress_user1 PASSWORD '12345!@#$%';

-- ok: letters + digits
ALTER USER regress_user1 PASSWORD 'abcdefg1';

-- ok: letters + specials
ALTER USER regress_user1 PASSWORD 'abcdefg!';

-- ok: uppercase + digits
ALTER USER regress_user1 PASSWORD 'ABCDEFG1';

-- ok: mixed case + digits + specials
ALTER USER regress_user1 PASSWORD 'ABcd12!@';

-- ===== 1D: Encrypted passwords (default policy) =====

-- ok: encrypted password (MD5 of "secret")
ALTER USER regress_user1 PASSWORD 'md51a44d829a20a23eac686d9f0d258af13';

-- error: encrypted password equals username
ALTER USER regress_user1 PASSWORD 'md5e589150ae7d28f93333afae92b36ef48';

-- ===== 1E: Various passing passwords (default policy) =====

-- ok: lowercase + digit
ALTER USER regress_user1 PASSWORD 'password1';

-- ok: lowercase + special
ALTER USER regress_user1 PASSWORD 'password!';

-- ok: uppercase + digit
ALTER USER regress_user1 PASSWORD 'PASSWORD1';

-- ok: mixed + specials
ALTER USER regress_user1 PASSWORD 'a_nice_long_password1';

-- ok: long password
ALTER USER regress_user1 PASSWORD 'ThisIsAVeryLongPassword123!@#';


-- ============================================================
-- Part 2: Strict policy (passwordcheck.strict_policy = on)
--   Rules: >= 9 chars, >= 2 uppercase, >= 2 lowercase,
--          >= 2 digits, >= 2 special, not contain username
-- ============================================================

SET passwordcheck.strict_policy = on;
SHOW passwordcheck.strict_policy;

-- ===== 2A: Password too short (minimum 9 characters) =====

-- error: 1 character
ALTER USER regress_user1 PASSWORD 'A';

-- error: 2 characters
ALTER USER regress_user1 PASSWORD 'AB';

-- error: 3 characters
ALTER USER regress_user1 PASSWORD 'ABc';

-- error: 4 characters
ALTER USER regress_user1 PASSWORD 'ABc1';

-- error: 5 characters
ALTER USER regress_user1 PASSWORD 'ABc1!';

-- error: 6 characters
ALTER USER regress_user1 PASSWORD 'ABc1!x';

-- error: 7 characters
ALTER USER regress_user1 PASSWORD 'ABc1!x2';

-- error: 8 characters (boundary - 1)
ALTER USER regress_user1 PASSWORD 'ABc1!x2@';

-- ok: exactly 9 characters (boundary, all conditions met)
ALTER USER regress_user1 PASSWORD 'ABcd12!@z';

-- ===== 2B: Password contains user name =====

-- error: username in the middle
ALTER USER regress_user1 PASSWORD 'AAregress_user1!!11';

-- error: username at start
ALTER USER regress_user1 PASSWORD 'regress_user1AA!!11';

-- error: username at end
ALTER USER regress_user1 PASSWORD 'AA!!11regress_user1';

-- error: username surrounded
ALTER USER regress_user1 PASSWORD 'XXregress_user1XX11!!';

-- ok: uppercase username (case-sensitive, no match)
ALTER USER regress_user1 PASSWORD 'REGRESS_USER1aa!!11';

-- error: username followed by digit
ALTER USER regress_user1 PASSWORD 'regress_user11A!a@b';

-- error: digits before username
ALTER USER regress_user1 PASSWORD '11regress_user1AA!!';

-- error: specials before username
ALTER USER regress_user1 PASSWORD '!!regress_user1AA11';

-- ===== 2C: Not enough uppercase letters (minimum 2) =====

-- error: 0 uppercase
ALTER USER regress_user1 PASSWORD 'abcdef12!@z';

-- error: 0 uppercase
ALTER USER regress_user1 PASSWORD 'abcdefgh!@12';

-- error: 1 uppercase (A at start)
ALTER USER regress_user1 PASSWORD 'Aabcdef12!@z';

-- error: 1 uppercase (Z in middle)
ALTER USER regress_user1 PASSWORD 'abcdeZf12!@z';

-- error: 1 uppercase (Z at end)
ALTER USER regress_user1 PASSWORD 'abcdef12!@zZ';

-- error: 0 uppercase, digits and specials first
ALTER USER regress_user1 PASSWORD '12345!!abc@dz';

-- error: 0 uppercase, specials first
ALTER USER regress_user1 PASSWORD '!@#abc12def3z';

-- error: 0 uppercase, lower+digit+special
ALTER USER regress_user1 PASSWORD 'abcd1234!@#$';

-- error: 0 uppercase, repeated lowercase
ALTER USER regress_user1 PASSWORD 'zzzzz99!!zzz';

-- error: 1 uppercase (Q at start)
ALTER USER regress_user1 PASSWORD 'Qxyz1234!!ab';

-- error: 1 uppercase (M in middle)
ALTER USER regress_user1 PASSWORD 'abcM12!!efgh';

-- error: 1 uppercase (K at end)
ALTER USER regress_user1 PASSWORD 'abcdef12!!gK';

-- ===== 2D: Not enough lowercase letters (minimum 2) =====

-- error: 0 lowercase
ALTER USER regress_user1 PASSWORD 'ABCDEF12!@ZZ';

-- error: 0 lowercase
ALTER USER regress_user1 PASSWORD 'ABCDEFGH!@12';

-- error: 1 lowercase (a in middle)
ALTER USER regress_user1 PASSWORD 'ABCDEFa12!@Z';

-- error: 1 lowercase (z in middle)
ALTER USER regress_user1 PASSWORD 'ABCDEz12!!FF';

-- error: 1 lowercase (x at end)
ALTER USER regress_user1 PASSWORD 'ABCDEF12!@Zx';

-- error: 0 lowercase, digits+specials+upper
ALTER USER regress_user1 PASSWORD '12345!!ABC@DZ';

-- error: 0 lowercase, specials first
ALTER USER regress_user1 PASSWORD '!@#ABC12DEF3Z';

-- error: 0 lowercase, upper+digit+special
ALTER USER regress_user1 PASSWORD 'ZZZZ1234!@#$';

-- error: 0 lowercase, repeated uppercase
ALTER USER regress_user1 PASSWORD 'QQQQQ99!!ZZZ';

-- error: 1 lowercase (m in middle)
ALTER USER regress_user1 PASSWORD 'ABCm12!!EFGH';

-- error: 1 lowercase (q at end)
ALTER USER regress_user1 PASSWORD 'AB12!!CDEFGq';

-- error: 0 lowercase, digit+special first
ALTER USER regress_user1 PASSWORD '1234!@ABCDEF';

-- ===== 2E: Not enough digits (minimum 2) =====

-- error: 0 digits
ALTER USER regress_user1 PASSWORD 'ABcd!@efghij';

-- error: 0 digits
ALTER USER regress_user1 PASSWORD 'AaBb!@CcDdEe';

-- error: 1 digit (1 in middle)
ALTER USER regress_user1 PASSWORD 'ABcd!@efgh1j';

-- error: 1 digit (5 in middle)
ALTER USER regress_user1 PASSWORD 'ABcdef!@g5hi';

-- error: 1 digit (0 at end)
ALTER USER regress_user1 PASSWORD 'ABcdef!@ghi0';

-- error: 0 digits, upper+lower+special
ALTER USER regress_user1 PASSWORD 'AABB!!ccddee';

-- error: 0 digits, specials scattered
ALTER USER regress_user1 PASSWORD '!@ABcd#$efgh';

-- error: 0 digits, mixed letters+specials
ALTER USER regress_user1 PASSWORD 'AAbb!!ccddff';

-- error: 0 digits, various specials
ALTER USER regress_user1 PASSWORD 'XYzw!@#$abcd';

-- error: 1 digit (9 at start)
ALTER USER regress_user1 PASSWORD '9ABcd!!efghz';

-- error: 1 digit (3 in middle)
ALTER USER regress_user1 PASSWORD 'ABcd!!efg3hz';

-- error: 1 digit (7 at end)
ALTER USER regress_user1 PASSWORD 'ABcd!!efghz7';

-- ===== 2F: Not enough special characters (minimum 2) =====

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'ABcd12efgh34';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'AABBccdd1234';

-- error: 1 special (! in middle)
ALTER USER regress_user1 PASSWORD 'ABcd12efg!hi';

-- error: 1 special (@ at end)
ALTER USER regress_user1 PASSWORD 'ABcdef12ghi@';

-- error: 1 special (! at start)
ALTER USER regress_user1 PASSWORD '!ABcdef12ghij';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'AABB1234ccdd';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'ZZaa99YYbbxx';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'Ab12Cd34Ef56';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'AAbb1234cdef';

-- error: 1 special (# in middle)
ALTER USER regress_user1 PASSWORD 'ABcd12efg#hi';

-- error: 0 special
ALTER USER regress_user1 PASSWORD 'ABcd1234efZx';

-- error: 1 special (^ at start)
ALTER USER regress_user1 PASSWORD '^ABcd12efghz';

-- ===== 2G: Multiple conditions fail (verify error priority) =====

-- error: too short (3 chars, also missing upper/digit/special)
ALTER USER regress_user1 PASSWORD 'aaa';

-- error: uppercase (10 specials, no upper/lower/digit)
ALTER USER regress_user1 PASSWORD '!!!!!!!!!!';

-- error: uppercase (0 upper, has lower+digit+special)
ALTER USER regress_user1 PASSWORD 'aaaaaa!!!!33';

-- error: lowercase (0 lower, has upper+digit+special)
ALTER USER regress_user1 PASSWORD 'AAAAAA!!!!33';

-- error: digits (0 digit, has upper+lower+special)
ALTER USER regress_user1 PASSWORD 'AAaa!!!!!!zz';

-- error: special (0 special, has upper+lower+digit)
ALTER USER regress_user1 PASSWORD 'AAaa1234zzzz';

-- error: uppercase (0 upper+lower, has digit+special)
ALTER USER regress_user1 PASSWORD '1234567890!!';

-- error: uppercase (all special, no upper/lower/digit)
ALTER USER regress_user1 PASSWORD '!!@@##$$%%^^';

-- error: lowercase (0 lower, 1 special, has upper+digit)
ALTER USER regress_user1 PASSWORD 'ABCDEFG1234!';

-- error: uppercase (0 upper, 1 special, has lower+digit)
ALTER USER regress_user1 PASSWORD 'abcdefg1234!';

-- ===== 2H: Boundary values that pass (exactly minimum) =====

-- ok: 9 chars, exactly 2+3+2+2
ALTER USER regress_user1 PASSWORD 'AAbb11!!z';

-- ok: 9 chars, different letters
ALTER USER regress_user1 PASSWORD 'ZZyy99@@a';

-- ok: 9 chars
ALTER USER regress_user1 PASSWORD 'QQww88##x';

-- ok: 9 chars
ALTER USER regress_user1 PASSWORD 'MMnn55$$p';

-- ok: 10 chars
ALTER USER regress_user1 PASSWORD 'XXff33&&hh';

-- ok: 11 chars
ALTER USER regress_user1 PASSWORD 'GGdd77**ee1';

-- ok: 12 chars
ALTER USER regress_user1 PASSWORD 'BBcc22!!dd33';

-- ok: 14 chars
ALTER USER regress_user1 PASSWORD 'JJkk44@@mm55!!';

-- ok: 10 chars
ALTER USER regress_user1 PASSWORD 'TTuu66^^ww';

-- ok: 10 chars
ALTER USER regress_user1 PASSWORD 'DDss00!!vv';

-- ===== 2I: Various passing passwords (strict) =====

-- ok: natural words
ALTER USER regress_user1 PASSWORD 'Hello99!!World';

-- ok: leet-style
ALTER USER regress_user1 PASSWORD 'P@55w0rD!!ab';

-- ok: mixed arrangement
ALTER USER regress_user1 PASSWORD 'CC!!dd33eeFF';

-- ok: digits first
ALTER USER regress_user1 PASSWORD '12AB!!cdef34';

-- ok: specials first
ALTER USER regress_user1 PASSWORD '!!AAbb1234cc';

-- ok: lowercase first
ALTER USER regress_user1 PASSWORD 'aaBB!!cc1234';

-- ok: many uppercase
ALTER USER regress_user1 PASSWORD 'ZZxx!!99qqWW';

-- ok: many specials
ALTER USER regress_user1 PASSWORD '##$$AAbb1234';

-- ok: 16 chars long
ALTER USER regress_user1 PASSWORD 'AB12cd!!ef34gh@@';

-- ok: many specials and digits
ALTER USER regress_user1 PASSWORD 'AAbb!!@@##1234cc';

-- ok: mixed pattern
ALTER USER regress_user1 PASSWORD 'XX99yy!!ZZaa';

-- ok: 4 uppercase
ALTER USER regress_user1 PASSWORD 'AABB99cc!@de';

-- ok: specials+upper+digit+lower alternating
ALTER USER regress_user1 PASSWORD '!!AB12cd34EF';

-- ok: sentence-like
ALTER USER regress_user1 PASSWORD 'My!!Pa55worD12';

-- ok: symmetric
ALTER USER regress_user1 PASSWORD 'QQ^^11yyWWzz';

-- ok: lowercase first mixed
ALTER USER regress_user1 PASSWORD 'ccDD!!9988ee';

-- ok: 2 upper + 4 lower
ALTER USER regress_user1 PASSWORD 'FFgg!!hh1234';

-- ok: specials at start
ALTER USER regress_user1 PASSWORD '@@ABcd1234ef';

-- ok: specials at end
ALTER USER regress_user1 PASSWORD 'ABcd1234ef@@';

-- ok: strong password
ALTER USER regress_user1 PASSWORD 'Str0ng!!P@55wd';

-- ===== 2J: Encrypted passwords (strict) =====

-- ok: encrypted password (MD5 of "secret")
ALTER USER regress_user1 PASSWORD 'md51a44d829a20a23eac686d9f0d258af13';

-- error: encrypted password equals username
ALTER USER regress_user1 PASSWORD 'md5e589150ae7d28f93333afae92b36ef48';

-- ===== 2K: Various special character types =====

-- ok: angle brackets
ALTER USER regress_user1 PASSWORD 'AAbb12<>efgz';

-- ok: curly braces
ALTER USER regress_user1 PASSWORD 'AAbb12{}efgz';

-- ok: square brackets
ALTER USER regress_user1 PASSWORD 'AAbb12[]efgz';

-- ok: parentheses
ALTER USER regress_user1 PASSWORD 'AAbb12()efgz';

-- ok: plus and minus
ALTER USER regress_user1 PASSWORD 'AAbb12+-efgz';

-- ok: tilde and backtick
ALTER USER regress_user1 PASSWORD 'AAbb12~`efgz';

-- ===== 2L: Passwords that pass default but fail strict =====

-- error: passes default (has letter+nonletter, >=8) but fails strict (0 uppercase)
ALTER USER regress_user1 PASSWORD 'password123!!';

-- error: passes default but fails strict (0 special)
ALTER USER regress_user1 PASSWORD 'ABcd12efgh34';

-- error: passes default but fails strict (1 uppercase)
ALTER USER regress_user1 PASSWORD 'Abcdefg12!!z';

-- error: passes default but fails strict (1 digit)
ALTER USER regress_user1 PASSWORD 'ABcd!!efghz7';

-- error: passes default but fails strict (8 chars, boundary)
ALTER USER regress_user1 PASSWORD 'ABcd12!@';

-- ===== Restore default and verify =====

SET passwordcheck.strict_policy = off;

-- ok: this was rejected under strict (0 uppercase) but passes default
ALTER USER regress_user1 PASSWORD 'password123!!';

DROP USER regress_user1;
