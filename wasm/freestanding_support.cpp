// フリースタンディング wasm32 ゲスト用ランタイム支援
//
// -nostdlib でリンクするため、libc / libc++abi のシンボルをここで提供する。
// - mojitonpp が使用する範囲では libc++ のヘッダ定義だけで完結しない
//   basic_string 等の実体 (libc++.a) を一部リンクするため、そこから参照される
//   libc 関数のうち、純粋なメモリ操作系は実装し、数値変換系は本ゲストの経路で
//   呼ばれないためトラップとして提供する。
// - operator new/delete は __heap_base から伸びるバンプアロケータ。
//   ponytail: 解放しない (delete は no-op)。使い捨てのバッチ処理を想定。
//   長時間稼働でアロケーションが多発するようであれば dlmalloc 等へ差し替える。
#include <stddef.h>
#include <stdint.h>

#include <string>

// libc++ は basic_string<char> の非インラインメンバを extern template 宣言して
// ライブラリ側 (libc++.a) の実体を要求する。ここで明示的実体化することで
// ヘッダの定義そのままの実体が生成され、libc++.a のリンクが不要になる。
// ponytail: libc++ バージョンとヘッダが一致している前提 (emsdk sysroot 使用)。
template class std::basic_string<char>;

// メンバテンプレートはクラスの明示的実体化では生成されないため個別に実体化する
template std::basic_string<char>& std::basic_string<char>::__assign_no_alias<false>(const char*, std::size_t);
template std::basic_string<char>& std::basic_string<char>::__assign_no_alias<true>(const char*, std::size_t);

// 例外無効化 (-fno-exceptions) 時に libc++ の __throw_* から呼ばれるアボート関数。
// std 名前空間 (ABI インライン名前空間) の内側で定義する必要がある。
#include <__verbose_abort>
_LIBCPP_BEGIN_NAMESPACE_STD
void __libcpp_verbose_abort(const char*, ...) noexcept { __builtin_trap(); }
_LIBCPP_END_NAMESPACE_STD

extern "C" unsigned char __heap_base[];

// ---------------------------------------------------------------- メモリ操作
extern "C" {
void* memcpy(void* dst, const void* src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  for (size_t i = 0; i < n; ++i) {
    d[i] = s[i];
  }
  return dst;
}

void* memmove(void* dst, const void* src, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  auto* s = static_cast<const unsigned char*>(src);
  if (d < s) {
    for (size_t i = 0; i < n; ++i) {
      d[i] = s[i];
    }
  } else {
    for (size_t i = n; i > 0; --i) {
      d[i - 1] = s[i - 1];
    }
  }
  return dst;
}

void* memset(void* dst, int c, size_t n) {
  auto* d = static_cast<unsigned char*>(dst);
  for (size_t i = 0; i < n; ++i) {
    d[i] = static_cast<unsigned char>(c);
  }
  return dst;
}

int memcmp(const void* lhs, const void* rhs, size_t n) {
  auto* a = static_cast<const unsigned char*>(lhs);
  auto* b = static_cast<const unsigned char*>(rhs);
  for (size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) {
      return a[i] < b[i] ? -1 : 1;
    }
  }
  return 0;
}

void* memchr(const void* s, int c, size_t n) {
  auto* p = static_cast<const unsigned char*>(s);
  for (size_t i = 0; i < n; ++i) {
    if (p[i] == static_cast<unsigned char>(c)) {
      return const_cast<unsigned char*>(p + i);
    }
  }
  return nullptr;
}

size_t strlen(const char* s) {
  size_t n = 0;
  while (s[n] != '\0') {
    ++n;
  }
  return n;
}
}  // extern "C"

// ------------------------------------------------------- アロケータ (バンプ)
namespace {
uintptr_t heap_ptr = reinterpret_cast<uintptr_t>(__heap_base);
constexpr uintptr_t k_page  = 65536U;
constexpr uintptr_t k_align = 16U;
}  // namespace

void* operator new(size_t n) {
  auto const size = (n + k_align - 1U) & ~(k_align - 1U);
  auto const mem_bytes = __builtin_wasm_memory_size(0) * k_page;
  if (heap_ptr + size > mem_bytes) {
    auto const pages = (heap_ptr + size - mem_bytes + k_page - 1U) / k_page;
    if (__builtin_wasm_memory_grow(0, pages) == static_cast<size_t>(-1)) {
      __builtin_trap();  // メモリ拡張失敗 (リミット到達)
    }
  }
  auto const p  = heap_ptr;
  heap_ptr += size;
  return reinterpret_cast<void*>(p);
}

void* operator new[](size_t n) { return ::operator new(n); }
void  operator delete(void*) noexcept {}
void  operator delete[](void*) noexcept {}
void  operator delete(void*, size_t) noexcept {}
void  operator delete[](void*, size_t) noexcept {}

// ------------------------------------------------------ 終了・エラー系
extern "C" void abort() { __builtin_trap(); }
extern "C" [[noreturn]] void __libcpp_verbose_abort(const char*, ...) { __builtin_trap(); }
extern "C" int* __errno_location() {
  static int errno_value = 0;
  return &errno_value;
}

// ------------------------------------------------ 未使用の libc 関数 (トラップ)
// libc++.a の basic_string 実体から参照されるが、本ゲストの実行経路では
// 呼ばれない。誤って呼ばれた場合は即座にトラップして問題を顕在化させる。
extern "C" int snprintf(char*, size_t, const char*, ...) { __builtin_trap(); }
extern "C" int swprintf(wchar_t*, size_t, const wchar_t*, ...) { __builtin_trap(); }
extern "C" double strtod(const char*, char**) { __builtin_trap(); }
extern "C" float strtof(const char*, char**) { __builtin_trap(); }
extern "C" long double strtold(const char*, char**) { __builtin_trap(); }
extern "C" long strtol(const char*, char**, int) { __builtin_trap(); }
extern "C" unsigned long strtoul(const char*, char**, int) { __builtin_trap(); }
extern "C" long long strtoll(const char*, char**, int) { __builtin_trap(); }
extern "C" unsigned long long strtoull(const char*, char**, int) { __builtin_trap(); }
extern "C" double wcstod(const wchar_t*, wchar_t**) { __builtin_trap(); }
extern "C" float wcstof(const wchar_t*, wchar_t**) { __builtin_trap(); }
extern "C" long double wcstold(const wchar_t*, wchar_t**) { __builtin_trap(); }
extern "C" long wcstol(const wchar_t*, wchar_t**, int) { __builtin_trap(); }
extern "C" unsigned long wcstoul(const wchar_t*, wchar_t**, int) { __builtin_trap(); }
extern "C" long long wcstoll(const wchar_t*, wchar_t**, int) { __builtin_trap(); }
extern "C" unsigned long long wcstoull(const wchar_t*, wchar_t**, int) { __builtin_trap(); }
extern "C" size_t wcslen(const wchar_t*) { __builtin_trap(); }
extern "C" wchar_t* wmemchr(const wchar_t*, wchar_t, size_t) { __builtin_trap(); }
extern "C" int wmemcmp(const wchar_t*, const wchar_t*, size_t) { __builtin_trap(); }
