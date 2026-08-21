#include <stdio.h>

/* Exercises OR/XOR/AND/NOT specifically -- the one place base-Nova
 * codegen genuinely differs from Eclipse. See NOVA_PORTING_NOTES.md:
 * targeting nova1/nova3/nova4 synthesizes OR/XOR from COM ("not") and
 * AND instead of emitting the Eclipse-only IOR/XOR instructions
 * directly. Expected output is identical regardless of -t target. */
int main(void) {
    unsigned int a = 5;
    unsigned int b = 3;
    printf("or=%d xor=%d and=%d not=%d\n", a | b, a ^ b, a & b, ~a);
    return 0;
}
