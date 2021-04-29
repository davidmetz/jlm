//
// Created by david on 4/5/21.
//

#define A(i,j) a[(i)*size+(j)]
#define B(i,j) b[(i)*size+(j)]
#define C_own(i,j) c[(i)*size+(j)]

void test( int *a, int *b, int *c, int size)
{
    for ( int i=0; i<size; i++ )
        for ( int j=0; j<size; j++ )
            for ( int k=0; k<size; k++ )
                C_own(i,j) += A(i,k) * B(k,j);
}