require 'mkmf'

if RUBY_ENGINE == 'truffleruby'
  # The pure-Ruby generator is faster on TruffleRuby, so skip compiling the generator extension
  File.write('Makefile', dummy_makefile("").join)
else
  append_cflags("-std=c99")
  $defs << "-DJSON_GENERATOR"

  if RbConfig::CONFIG['host_cpu'] =~ /^(arm.*|aarch64.*)/
    # Try to compile a small program using NEON instructions
    have_header('arm_neon.h') && try_compile(<<~'END_SRC')
      #include <arm_neon.h>
      int main() {
          uint8x16_t test = vdupq_n_u8(32);
          return 0;
      }
    END_SRC
  end

  create_makefile 'json/ext/generator'
end
