//
// Created by david on 4/5/21.
//

#define A(i,j) a[(i)*size+(j)]

void test( int *a, int size)
{
    for ( int i=0; i<size; i++ )
        for ( int j=0; j<size; j++ )
            A(i,j) += 1;
}