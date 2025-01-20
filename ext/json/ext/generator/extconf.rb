require 'mkmf'

if RUBY_ENGINE == 'truffleruby'
  # The pure-Ruby generator is faster on TruffleRuby, so skip compiling the generator extension
  File.write('Makefile', dummy_makefile("").join)
else
  append_cflags("-std=c99")
  $defs << "-DJSON_GENERATOR"

  if enable_config('use-simd', default=true)
    if RbConfig::CONFIG['host_cpu'] =~ /^(arm.*|aarch64.*)/
      $defs.push("-DENABLE_SIMD")

      # Try to compile a small program using NEON instructions
      if have_header('arm_neon.h')
        have_type('uint8x16_t', headers=['arm_neon.h']) && try_compile(<<~'SRC')
          #include <arm_neon.h>
          int main() {
              uint8x16_t test = vdupq_n_u8(32);
              return 0;
          }
        SRC

        have_type('uint8x8_t', headers=['arm_neon.h']) && try_compile(<<~'SRC')
            #include <arm_neon.h>
            int main() {
                uint8x8_t test = vdup_n_u8(32);
                return 0;
            }
        SRC
        end
      elsif have_header('x86intrin.h')
        
        # This is currently hardcoded to false as using m256 seems significantly slower on my machine.
        # TODO make this configurable
        if false && have_type('__m256i', headers=['x86intrin.h']) && try_compile(<<~'SRC', opt='-mavx2')
          #include <x86intrin.h>
          int main() {
              __m256i test = _mm256_set1_epi8(32);
              return 0;
          }
          SRC
          $defs.push("-DENABLE_SIMD")
        elsif have_type('__m128i', headers=['x86intrin.h']) && try_compile(<<~'SRC', opt='-mavx2')
          #include <x86intrin.h>
          int main() {
              __m128i test = _mm_set1_epi8(32);
              return 0;
          }
          SRC
            $defs.push("-DENABLE_SIMD")
        end
      end

      have_header('cpuid.h')
  end

  create_header
  create_makefile 'json/ext/generator'
end
