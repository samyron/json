case RbConfig::CONFIG['host_cpu']
when /^(arm|aarch64)/
  # Try to compile a small program using NEON instructions
  header, type, init, extra = 'arm_neon.h', 'uint8x16_t', 'vdupq_n_u8(32)', nil
when /^(x86_64|x64)/
  header, type, init, extra = 'x86intrin.h', '__m128i', '_mm_set1_epi8(32)', 'if (__builtin_cpu_supports("sse2")) { printf("OK"); }'
end
# intrin.h
if header
  if have_header(header) && try_compile(<<~SRC, '-Werror=implicit-function-declaration')
      #{cpp_include(header)}
      int main(int argc, char **argv) {
        #{type} test = #{init};
        #{extra}
        if (argc > 100000) printf("%p", &test);
        return 0;
      }
    SRC
    $defs.push("-DJSON_ENABLE_SIMD")
  else
    if RbConfig::CONFIG['host_os'] =~ /mswin/i
        if have_header('intrin.h') && try_compile(<<~SRC, '-arch:SSE2')
          #include <intrin.h>
          int main() { __m128i x = _mm_setzero_si128(); return 0; }
        SRC
        $defs.push("-DJSON_ENABLE_SIMD")
      end
    else
      puts "Disable SIMD"
    end
  end
end

have_header('cpuid.h')
