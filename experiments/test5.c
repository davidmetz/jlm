//
// Created by david on 4/5/21.
//

unsigned int test(int*  a, unsigned int count){
//    *a+=b;
    long sum = 0;
    for (int i = 0; i < count; ++i) {
        sum += a[i];
    }
    return sum;
}