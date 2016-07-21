#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "filtrar.h"

/* Este filtro deja pasar los caracteres NO alfabeticos. */
/* Devuelve el numero de caracteres que han pasado el filtro. */
int tratar(char* buffer_in, char* buffer_out, int tamano){

	int i;
	int numero = 0;
	int output = 0; 

 	for(i=0; i<tamano; i++){

 		if(!isalpha(buffer_in[i])){ 
 			buffer_out[output]=buffer_in[i];
 			output++; 
 			numero++;
 		}
	}

 	return numero;
}
