#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h> 
#include <dirent.h>     
#include <sys/stat.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
 
#include "filtrar.h"

/* ---------------- PROTOTIPOS ----------------- */ 
/* Esta funcion monta el filtro indicado y busca el simbolo "tratar"
   que debe contener, y aplica dicha funcion "tratar()" para filtrar
   toda la informacion que le llega por su entrada estandar antes
   de enviarla hacia su salida estandar. */
extern void filtrar_con_filtro(char* nombre_filtro);

/* Esta funcion lanza todos los procesos necesarios para ejecutar los filtros.
   Dichos procesos tendran que tener redirigida su entrada y su salida. */
void preparar_filtros(void);

/* Esta funcion recorrera el directorio pasado como argumento y por cada entrada
   que no sea un directorio o cuyo nombre comience por un punto '.' la lee y 
   la escribe por la salida estandar (que seria redirigida al primero de los 
   filtros, si existe). */
void recorrer_directorio(char* nombre_dir);

/* Esta funcion recorre los procesos arrancados para ejecutar los filtros, 
   esperando a su terminacion y recogiendo su estado de terminacion. */ 
void esperar_terminacion(void);

/* Desarrolle una funcion que permita controlar la temporizacion de la ejecucion
   de los filtros. */ 
extern void preparar_alarma(void);

/* Función de tratamiento de la alarma SIGALRM */
void funcion_tratar();

/* ---------------- IMPLEMENTACIONES ----------------- */ 
char** filtros;   /* Lista de nombres de los filtros a aplicar */
int    n_filtros; /* Tama~no de dicha lista */
pid_t* pids;      /* Lista de los PIDs de los procesos que ejecutan los filtros */
int i; 			  /* Variable para los bucles for */
int pp[2]; 		  /* Array para la creación de los pipes */
int status;		  /* Variables status */
char *tamano;	  /* Variable tamano para la funcion filtrar_con_filtro */
char *timeout; 	  /* Variable para guardar la variable de entorno "FILTRAR_TIMEOUT" */
int tiempo; 	  /* Variable para guardar el tiempo a poner en la alarma */

/* Funcion principal */
int main(int argc, char* argv[]){

	/* Chequeo de argumentos */
	if(argc < 2){
		/* Invocacion sin argumentos  o con un numero de argumentos insuficiente */
		fprintf(stderr, "Uso: %s directorio [filtro...]\n", argv[0]);
		exit(1);
	}

	filtros = &(argv[2]);                             /* Lista de filtros a aplicar */
	n_filtros = argc-2;                               /* Numero de filtros a usar */
	pids = (pid_t*)malloc(sizeof(pid_t)*n_filtros);   /* Lista de pids */

	/* Armado de la señal SIGALRM */
	struct sigaction act;
 	act.sa_handler = &funcion_tratar;
    act.sa_flags = SA_SIGINFO;
    sigaction(SIGALRM, &act, NULL);
 
 	/* Recogemos la varibale de entorno */
    timeout = getenv("FILTRAR_TIMEOUT");

    if (timeout != NULL){
		preparar_alarma();
	}

	preparar_filtros();

	recorrer_directorio(argv[1]);

	esperar_terminacion();

	return 0;
}

void recorrer_directorio(char* nombre_dir){

	DIR* dir = NULL;
	struct dirent* ent;
	struct stat statuStat;
	char fich[1024];
	char aux[1024];
	char buff[4096];
	int fd;

	/* Abrir el directorio y Tratamiento del error */
	if((dir = opendir(nombre_dir)) == NULL) {
		fprintf(stderr,"Error al abrir el directorio '%s'\n", nombre_dir);
		exit(1);
	}

	if((ent = readdir(dir)) == NULL){
		fprintf(stderr,"Error al leer el directorio '%s'\n",nombre_dir);
        exit(1);
	}

	rewinddir(dir);

	/* Recorremos las entradas del directorio */
	while((ent = readdir(dir))!= NULL){

		/* Nos saltamos las que comienzan por un punto "." */
		if(ent->d_name[0]=='.')
			continue;

		/* fich debe contener la ruta completa al fichero */
		getcwd(fich, 1024);
		strcat(fich, "/");
		sprintf(aux,"%s/%s", nombre_dir, ent->d_name);
		strcat(fich, aux);

		/* Nos saltamos las rutas que sean directorios. */
		if(stat(fich, &statuStat) < 0){
            fprintf(stderr,"AVISO: No se puede stat el fichero '%s'!\n", aux);
            continue;
	    }

	    if(S_ISDIR(statuStat.st_mode)){
 			continue;
 		}
			
		/* Abrir el archivo y Tratamiento del error. */
		if((fd = open(fich, O_RDONLY)) == -1) {
			fprintf(stderr, "AVISO: No se puede abrir el fichero '%s'!\n", aux);
			continue;
		}

		/* Cuidado con escribir en un pipe sin lectores! */


		/* Emitimos el contenido del archivo por la salida estandar. */
		while(write(1, buff, read(fd, buff, 4096)) > 0)
			continue;

		/* Cerrar. */
		close(fd);

	}

	/* Cerrar. */
	closedir(dir);
	close(1);

	/* IMPORTANTE:
	 * Para que los lectores del pipe puedan terminar
	 * no deben quedar escritores al otro extremo. */
	// IMPORTANTE
}

void preparar_filtros(void){

	int p;
	
	for(i = n_filtros; i > 0; i--){

		/* Tuberia hacia el hijo (que es el proceso que filtra). */
		if(pipe(pp) < 0){
			fprintf(stderr, "Error al crear el pipe\n");
			exit(1);
		}

		/* Lanzar nuevo proceso */
		switch(p = fork()){

			case -1: /* Error. Mostrar y terminar. */

				fprintf(stderr, "Error al crear proceso %d\n", p);
				exit(1);

			case  0: /* Hijo: Redireccion y Ejecuta el filtro. */
				
				signal(SIGALRM, SIG_IGN);

				close(pp[1]);
				dup2(pp[0], 0);
				close(pp[0]);

				tamano = strrchr(filtros[i-1], '.');
				/* El nombre termina en ".so" ? */
				if (tamano != NULL && strcmp(tamano, ".so") == 0){
				/* SI. Montar biblioteca y utilizar filtro. */
					filtrar_con_filtro(filtros[i-1]);
					exit(0);
				}

				else {	
				/* NO. Ejecutar como mandato estandar. */
					execvp(filtros[i-1], filtros);
					fprintf(stderr, "Error al ejecutar el mandato '%s'\n", filtros[i-1]);
					exit(1);

				}
				break;
		
			default: /* Padre: Redireccion */
				
				signal(SIGALRM, SIG_IGN);

				close(pp[0]);
				dup2(pp[1], 1);
				close(pp[1]);

				break;
		}
	}
}

void imprimir_estado(char* filtro, int status){

	/* Imprimimos el nombre del filtro y su estado de terminacion */
	if(WIFEXITED(status))
		fprintf(stderr,"%s: %d\n", filtro, WEXITSTATUS(status));
	else
		fprintf(stderr,"%s: senal %d\n", filtro, WTERMSIG(status));
}

void esperar_terminacion(void){

    int p;
    
    for(p = 0; p < n_filtros; p++){

		/* Espera al proceso pids[p] */
    	if(waitpid(pids[p], &status, 0) < 0){
            fprintf(stderr,"Error al esperar proceso %d\n", pids[p]);
            exit(1);
        }

		/* Muestra su estado. */
        imprimir_estado(filtros[n_filtros-1-p], status);

    }
}

void filtrar_con_filtro(char* nombre_filtro){

 	void *biblioteca;
 	int (*carga)(char *buff_in, char *buff_out, int tam);
 	char fich[1024];
 	char buffer_in[4096];
 	char buffer_out[4096];
 	char* msgerror;
 	int solucion;

 	strcpy(fich, nombre_filtro);
 	biblioteca = dlopen(fich, RTLD_LAZY);

 	if (biblioteca == NULL){
 		fprintf(stderr, "Error al abrir la biblioteca '%s'\n", nombre_filtro);
 		exit(1);
 	}

 	dlerror();
 	*(void **) (&carga) = dlsym(biblioteca, "tratar");

 	if((msgerror = dlerror())!= NULL){
 		fprintf(stderr, "Error al buscar el simbolo '%s' en '%s'\n", "tratar", nombre_filtro);
 		exit(1);
 	}

 	while((solucion = read(0, buffer_in, 4096)) > 0){
 		(*carga)(buffer_in, buffer_out, solucion);
 		write(1, buffer_out, solucion);
 	}

 	dlclose(biblioteca);
}

void funcion_tratar(){
    
    int i;

    for(i = n_filtros-1; i >= 0; i--){

        if(kill(pids[i],0)){

            if(kill(pids[i],9) < 0){
                fprintf(stderr,"Error al intentar matar proceso %d\n", pids[i]);
                exit(1);
            }
       
        }
        exit(0);
    }
}

void preparar_alarma(void){
 
    tiempo = strtol(timeout, NULL, 10);
    
    if(tiempo > 0){
		alarm(tiempo);
        printf("AVISO: La alarma vencera tras '%d' segundos!\n", tiempo);
    }

    else{
    	fprintf(stderr, "Error FILTRAR_TIMEOUT no es entero positivo:\n");
    	exit(1);
    } 
}

