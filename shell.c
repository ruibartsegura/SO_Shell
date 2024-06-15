#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

enum {
	MAX_LONG = 1024,
	STD_IO = -1,
	NULL_IN = -2,
};

void
ejecutar_cmd(char *cmd, char **args)
{
	execv(cmd, args);	// Ejecutar el comando ping
	// Si execv() devuelve, ha habido un error
	perror("execv");
	exit(EXIT_FAILURE);
}

void
redir_cmd(char **comando, int in_fd, int out_fd)
{
	char accion[MAX_LONG] = "/bin/";

	switch (fork()) {
	case -1:
		err(EXIT_FAILURE, "cannot fork");
	case 0:
		// Redirigir entrada estándar
		if (in_fd > STD_IO) {
			dup2(in_fd, STDIN_FILENO);
			close(in_fd);
		} else if (in_fd == NULL_IN) {
			in_fd = open("/dev/null", O_RDONLY);
			if (in_fd == -1) {
				perror("Error al abrir /dev/null");
				exit(EXIT_FAILURE);
			}
			close(in_fd);
		}

		if (out_fd > STD_IO) {
			dup2(out_fd, STDOUT_FILENO);
			close(out_fd);
		}
		// Ejecutar cmd
		strcat(accion, comando[0]);
		ejecutar_cmd(accion, comando);
	}
}

void
preparar_cmd(char **comando, int pipes, int redir_in, int redir_out, int shh)
{
	//printf("pipes %d, redir %d, shh %d\n", pipes, redir, shh);

	if (pipes == 0 && redir_in == 0 && redir_out == 0) {
		int in_fd = STD_IO, out_fd = STD_IO;

		if (shh != 0 && redir_in == 0)
			in_fd = NULL_IN;

		redir_cmd(comando, in_fd, out_fd);

	} else if (pipes != 0 && redir_in == 0 && redir_out == 0) {
		int i = 0, j = 0, num_cmd = 0;
		int in_fd = STD_IO, out_fd = STD_IO;
		int fd[2];

		// Reservar mem y limpiarla
		char **cmd = malloc(MAX_LONG * sizeof(char *));

		memset(cmd, 0, MAX_LONG * sizeof(char *));

		//Separamos cada comando del pipe y lo ejecutamos
		while (comando[i] != NULL) {
			// Poner en cmd las partes del pipe por separado
			if (strcmp(comando[i], "|") != 0) {
				cmd[j++] = comando[i];
			} else {
				if (pipe(fd) < 0)
					err(EXIT_FAILURE, "cannot make a pipe");

				if (shh != 0 && redir_in == 0 && num_cmd == 0)
					in_fd = NULL_IN;

				redir_cmd(cmd, in_fd, fd[1]);

				// Cerrar el extremo de escritura y la entrada anterior
				close(fd[1]);
				if (in_fd != -1)
					close(in_fd);
				in_fd = fd[0];
				// Resetear memoria de cmd
				memset(cmd, 0, MAX_LONG * sizeof(char *));
				j = 0;
				num_cmd++;
			}
			i++;
		}
		if (pipe(fd) < 0)
			err(EXIT_FAILURE, "cannot make a pipe");
		// Cerrar el extremo de escritura del último pipe
		close(fd[1]);
		redir_cmd(cmd, in_fd, out_fd);	// Ejecutar el último comando sin redirección de salida

		free(cmd);
		close(fd[0]);
		close(fd[1]);
		close(in_fd);

	} else if (pipes == 0 && (redir_in != 0 || redir_out != 0)) {
		int i = 0, j = 0;

		int in_fd = STD_IO, out_fd = STD_IO;
		char *in_path, *out_path;

		// Reservar mem y limpiarla
		char **cmd = malloc(MAX_LONG * sizeof(char *));

		memset(cmd, 0, MAX_LONG * sizeof(char *));
		// El cmd llega hasta la primera redireccion
		while (comando[i] != NULL) {
			if (strcmp(comando[i], "<") != 0
			    && strcmp(comando[i], ">") != 0) {
				cmd[i] = comando[i];
				i++;
			} else {
				// Comprobar si tiene redir de entrada y cual es
				if (redir_in != 0) {
					j = 0;
					while (comando[j] != NULL) {
						if (strcmp(comando[j], "<") ==
						    0) {
							// Siguiente token al < es la entrada
							in_path = comando[++j];
						}
						j++;
					}
					in_fd = open(in_path, O_RDONLY, 0400);
					if (in_fd == -1) {
						perror
						    ("Error al abrir el fichero de entrada");
						close(in_fd);
						exit(EXIT_FAILURE);
					}
				} else if (shh != 0 && redir_in == 0) {
					in_fd = NULL_IN;
				}
				// Comprobar si tiene redir de salida y cual es
				if (redir_out != 0) {
					j = 0;
					while (comando[j] != NULL) {
						if (strcmp(comando[j], ">") ==
						    0) {
							// Siguiente token al > es la salida
							out_path = comando[++j];
						}
						j++;
					}
					out_fd =
					    open(out_path,
						 O_RDWR | O_CREAT | O_TRUNC,
						 0600);
					if (out_fd == -1) {
						perror
						    ("Error al abrir o crear el fichero destino%s\n");
						close(out_fd);
						exit(EXIT_FAILURE);
					}
				}
				redir_cmd(cmd, in_fd, out_fd);
				break;
			}
		}
		close(in_fd);
		close(out_fd);
		free(cmd);

	} else if (pipes != 0 && (redir_in != 0 || redir_out != 0)) {
		int i = 0, j = 0;
		int in_fd = STD_IO, out_fd = STD_IO;

		// Para saber si es el primer comando o no
		int num_cmd = 0;
		int fd[2];
		char *in_path, *out_path;

		// Reservar mem y limpiarla
		char **cmd = malloc(MAX_LONG * sizeof(char *));

		memset(cmd, 0, MAX_LONG * sizeof(char *));

		// Separamos cada comando del pipe y lo ejecutamos
		while (strcmp(comando[i], "<") != 0
		       && strcmp(comando[i], ">") != 0) {
			// Poner en cmd las partes del pipe por separado
			if (strcmp(comando[i], "|") != 0) {
				cmd[j++] = comando[i];
			} else {
				if (pipe(fd) < 0)
					err(EXIT_FAILURE, "cannot make a pipe");

				// Comprobar si tiene redir de entrada y cual es
				if (num_cmd == 0 && redir_in != 0) {
					int k = 0;

					while (comando[k] != NULL) {
						if (strcmp(comando[k], "<") ==
						    0) {
							// Siguiente token al < es la entrada
							in_path = comando[++k];
						}
						k++;
					}

					in_fd = open(in_path, O_RDONLY, 0400);
					if (in_fd == -1) {
						perror
						    ("Error al abrir el fichero de entrada");
						close(in_fd);
						exit(EXIT_FAILURE);
					}
				} else if (shh != 0 && redir_in == 0
					   && num_cmd == 0) {
					in_fd = NULL_IN;
				}

				redir_cmd(cmd, in_fd, fd[1]);

				// Cerrar el extremo de escritura y la entrada anterior
				close(fd[1]);
				if (in_fd != -1)
					close(in_fd);
				in_fd = fd[0];
				// Resetear memoria de cmd
				memset(cmd, 0, MAX_LONG * sizeof(char *));
				j = 0;
				num_cmd++;
			}
			i++;
		}
		if (pipe(fd) < 0)
			err(EXIT_FAILURE, "cannot make a pipe");

		// Cerrar el extremo de escritura del último pipe
		close(fd[1]);

		// Comprobar si tiene redir de salida y cual es
		if (redir_out != 0) {
			int k = 0;

			while (comando[k] != NULL) {
				if (strcmp(comando[k], ">") == 0) {
					// Siguiente token al > es la salida
					out_path = comando[++k];
				}
				k++;
			}

			out_fd =
			    open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0600);
			if (out_fd == -1) {
				perror
				    ("Error al abrir o crear el fichero destino%s\n");
				close(out_fd);
				exit(EXIT_FAILURE);
			}
		} else {
			out_fd = STD_IO;
		}
		redir_cmd(cmd, in_fd, out_fd);	// Ejecutar el último comando sin redirección de salida

		free(cmd);
		close(fd[0]);
		close(fd[1]);
		close(in_fd);
		close(out_fd);
	}
	if (shh == 0) {
		while (wait(NULL) > 0) ;
	}
}

char **
extraer_tokens(char *entrada)
{
	char **token = malloc(MAX_LONG * sizeof(char *));

	memset(token, 0, MAX_LONG * sizeof(char *));

	if (!token) {
		fprintf(stderr, "Error al asignar memoria\n");
		exit(EXIT_FAILURE);
	}
	char *delim = " \n\t";
	char *saveptr;

	int position = 0;

	// Primera llamada a strtok_r con la cadena original
	token[position] = strtok_r(entrada, delim, &saveptr);
	while (token[position] != NULL) {
		if (position >= MAX_LONG) {
			fprintf(stderr, "Demasiados argumentos\n");
			exit(EXIT_FAILURE);
		}
		position++;

		// Llamadas subsecuentes a strtok_r con NULL
		token[position] = strtok_r(NULL, delim, &saveptr);
	}
	// El ultimo token es null para saber que se termino de leer
	token[position + 1] = NULL;
	return token;
}

void
leer_entrada(char **entrada)
{
	int pipes, redir_in, redir_out, shh;

	pipes = 0;
	redir_in = 0;
	redir_out = 0;
	shh = 0;

	for (int i = 0; entrada[i] != NULL; i++) {
		if (strcmp(entrada[i], "|") == 0 && redir_in == 0
		    && redir_out == 0 && shh == 0) {
			pipes++;
		} else if (strcmp(entrada[i], "|") == 0
			   && (redir_in != 0 || redir_out != 0 || shh != 0)) {
			fprintf(stderr, "Sintaxis del comando erronea\n");
			return;
		}

		if (strcmp(entrada[i], "<") == 0 && shh == 0) {
			redir_in++;
		} else if (strcmp(entrada[i], ">") == 0 && shh == 0) {
			redir_out++;
		} else
		    if ((strcmp(entrada[i], "<") == 0
			 || strcmp(entrada[i], ">") == 0) && shh != 0) {
			fprintf(stderr, "Sintaxis del comando erronea\n");
			return;
		}

		if (strcmp(entrada[i], "&") == 0 && entrada[i + 1] == NULL) {
			entrada[i] = NULL;
			shh++;
		} else if ((strcmp(entrada[i], "&") == 0)
			   && entrada[i + 1] != NULL) {
			fprintf(stderr, "Sintaxis del comando erronea\n");
			return;
		}
	}
	preparar_cmd(entrada, pipes, redir_in, redir_out, shh);
}

int
main(int argc, char *argv[])
{
	argc--;
	char entrada[MAX_LONG];
	char **token;

	if (argc != 0) {
		errx(EXIT_FAILURE,
		     "Argumentos esperados 0, argumentos recibidos %d\n", argc);
	}

	while (fgets(entrada, MAX_LONG, stdin) != NULL) {
		token = extraer_tokens(entrada);

		leer_entrada(token);

		free(token);
	}
	exit(EXIT_SUCCESS);
}
