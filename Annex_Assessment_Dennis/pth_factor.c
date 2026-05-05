#include <stdio.h>

long long pthFactor(long long n, long long p) {
    long long small_factor_count = 0;
    long long sqrt_floor = 0;

    for (long long factor = 1; factor <= n / factor; factor++) {
        sqrt_floor = factor;
        if (n % factor != 0) {
            continue;
        }

        small_factor_count++;
        if (small_factor_count == p) {
            return factor;
        }
    }

    long long remaining = p - small_factor_count;
    for (long long factor = sqrt_floor; factor >= 1; factor--) {
        if (n % factor != 0 || factor == n / factor) {
            continue;
        }

        remaining--;
        if (remaining == 0) {
            return n / factor;
        }
    }

    return 0;
}

int main(void) {
    long long n;
    long long p;

    if (scanf("%lld %lld", &n, &p) != 2) {
        return 1;
    }

    printf("%lld\n", pthFactor(n, p));
    return 0;
}
