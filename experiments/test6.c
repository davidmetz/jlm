//
// Created by david on 4/5/21.
//

#define A(i,j) a[(i)*size+(j)]
#define B(i,j) b[(i)*size+(j)]
#define C_own(i,j) c[(i)*size+(j)]

void test( int *a, int *b, int *c, int size)
{
//    for ( int i=0; i<size; i++ )
//        for ( int j=0; j<size; j++ )
//            for ( int k=0; k<size; k++ )
//                C_own(i,j) += A(i,k) * B(k,j);

    // TODO: how do we automatically transform the form above into the form below?

//    int i = 0;
//    if(i<size) {
//        do {
//            int j = 0;
//            do {
//                int k = 0;
//                do {
//                    C_own(i,j) += A(i,k) * B(k,j);
//                    k++;
//                } while (k<size);
//                j++;
//            } while (j<size);
//            i++;
//        } while (i<size);
//    }

    int i = 0;
    if(i<size) {
        do {
            int j = 0;
            do {
                int sum = C_own(i,j);
                int k = 0;
                do {
                    sum += A(i,k) * B(k,j);
                    k++;
                } while (k<size);
                C_own(i,j) = sum;
                j++;
            } while (j<size);
            i++;
        } while (i<size);
    }

//    int i = 0;
//    if(i<size) {
//        do {
//            int sum = c[i];
//            int k = 0;
//            do {
//                sum += a[k];
//                k++;
//            } while (k<size);
//            c[i] = sum;
//            i++;
//        } while (i<size);
//    }

//    int test = 0;
//    int i = 0;
//    if(i<size) {
//        do {
//            test += i;
//            int k = 0;
//            do {
//                test += k;
//                k++;
//            } while (k<size);
//            c[i] = test;
//            test += 5;
//            i++;
//        } while (i<size);
//    }
}