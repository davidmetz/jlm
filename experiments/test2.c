//
// Created by david on 4/5/21.
//

unsigned int test(unsigned int a, unsigned int b){
    //gcd
    while(a!=b){
        if(a>b)
            a -= b;
        else
            b -= a;
    }
//    do{
//        a--;
//    } while (a!=0);
    return a;
}