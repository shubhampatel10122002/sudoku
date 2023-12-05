#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/wait.h>

#define GRID_SIZE 9
#define SUBGRID_SIZE 3

int puzzle[GRID_SIZE][GRID_SIZE];

sem_t row_semaphores[GRID_SIZE];
sem_t column_semaphores[GRID_SIZE];
sem_t subgrid_semaphores[GRID_SIZE][GRID_SIZE];

// Function to load the Sudoku from input file
void loadSudoku(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        perror("Error opening file");
        exit(1);
    }

    char buffer[GRID_SIZE * GRID_SIZE + 1]; 
    
    for (int i = 0; i < GRID_SIZE; i++) {
        if (fscanf(file, "%s", buffer) != 1) {
            fprintf(stderr, "Error reading puzzle from file\n");
            fclose(file);
            exit(1);
        }

        for (int j = 0; j < GRID_SIZE; j++) {
            if (buffer[j] >= '0' && buffer[j] <= '9') {
                puzzle[i][j] = buffer[j] - '0';
            } 
            else {
                fprintf(stderr, "Invalid character in puzzle\n");
                fclose(file);
                exit(1);
            }
        }
    }

    fclose(file);
}

// Function to save the solution in output file
void saveSudoku(const char *filename) {
    FILE *file = fopen(filename, "w");
    if (file == NULL) {
        perror("Error opening file");
        exit(1);
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {
            fprintf(file, "%d", puzzle[i][j]);
        }
        fprintf(file, "\n");
    }

    fclose(file);
}

// Function for initializing semaphores
void initializeSemaphores() {
    for (int i = 0; i < GRID_SIZE; i++) {
        sem_init(&row_semaphores[i], 0, 1);
        sem_init(&column_semaphores[i], 0, 1);
    }

    for (int i = 0; i < GRID_SIZE; i += SUBGRID_SIZE) {
        for (int j = 0; j < GRID_SIZE; j += SUBGRID_SIZE) {
            sem_init(&subgrid_semaphores[i][j], 0, 1);
        }
    }
}

// Function for destroying semaphores
void destroySemaphores() {
    for (int i = 0; i < GRID_SIZE; i++) {
        sem_destroy(&row_semaphores[i]);
        sem_destroy(&column_semaphores[i]);
    }

    for (int i = 0; i < GRID_SIZE; i += SUBGRID_SIZE) {
        for (int j = 0; j < GRID_SIZE; j += SUBGRID_SIZE) {
            sem_destroy(&subgrid_semaphores[i][j]);
        }
    }
}

// Function to count the number of zeros in the puzzle array
int countZeros() {
    int zeroCount = 0;
    
    for (int row = 0; row < GRID_SIZE; row++) {
        for (int col = 0; col < GRID_SIZE; col++) {
            if (puzzle[row][col] == 0) {
                zeroCount++;
            }
        }
    }
    
    return zeroCount;
}

// Function for sudoku solver
int solveSudoku(int row, int column) {

    if (row == GRID_SIZE) {
        return 1;
    }

    if (column == GRID_SIZE) {
        return solveSudoku(row + 1, 0);
    }

    if (puzzle[row][column] != 0) {
        return solveSudoku(row, column + 1);
    }

    int subgrid_row = row / SUBGRID_SIZE * SUBGRID_SIZE;
    int subgrid_col = column / SUBGRID_SIZE * SUBGRID_SIZE;

    for (int num = 1; num <= GRID_SIZE; num++) {

        // Check if the number is valid in this cell
        int valid = 1;
        for (int i = 0; i < GRID_SIZE && valid; i++) {
            if (puzzle[row][i] == num ||
                puzzle[i][column] == num ||
                puzzle[subgrid_row + i / SUBGRID_SIZE][subgrid_col + i % SUBGRID_SIZE] == num) {
                valid = 0;
            }
        }
        
        if (valid) {
            
            // Acquire the write access of cell
            sem_wait(&row_semaphores[row]);
            sem_wait(&column_semaphores[column]);
            sem_wait(&subgrid_semaphores[subgrid_row][subgrid_col]);
            
            puzzle[row][column] = num;
            
            // Release the access
            sem_post(&row_semaphores[row]);
            sem_post(&column_semaphores[column]);
            sem_post(&subgrid_semaphores[subgrid_row][subgrid_col]);
            
            int nextRow = row;
            int nextCol = column + 1;
            if (nextCol == GRID_SIZE) {
                nextRow++;
                nextCol = 0;
            }
            if (solveSudoku(nextRow, nextCol)){
            	return 1;
            }

            // Backtrack
            puzzle[row][column] = 0;
        }
    }
    return 0;
}

int check(int puzzle[9][9]) {
    for (int i = 0; i < 9; i++) {
        for (int j = 0; j < 9; j++) {
            if (puzzle[i][j] == 0) {
            	return 0;
            }
        }
    }
    return 1;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <input_file>\n", argv[0]);
        return 1;
    }

    loadSudoku(argv[1]);

    // Initialize semaphores
    initializeSemaphores();
    
    int num_processes = countZeros(); // Count the number of zeros in the puzzle array
    int f = 0;
    int child_exit_status[num_processes];
    pid_t pid[num_processes];

    int process_index = 0;

    for (int row = 0; row < GRID_SIZE; row++) {
	    for (int col = 0; col < GRID_SIZE; col++) {
		if (puzzle[row][col] == 0) {
		    pid[process_index] = fork();

		    // Child process
		    if (pid[process_index] == 0) {
		        // Set the starting position for this child process
		        if (solveSudoku(row, col) == 1) {
		            f = check(puzzle);
		            if (f) {
		                saveSudoku("output.txt");
		                destroySemaphores();
		                exit(0);
		            }
		        } 
		        else {
		            exit(1);
		        }
		    }
		    // Fork failed
		    else if (pid[process_index] < 0) {
		        printf("Fork failed.\n");
		        exit(1);
		    }

		    process_index++;
		}
	    }
    }
    
    int status;
    int child_found = 0;

    for (int i = 0; i < num_processes; i++) {
     	waitpid(pid[i], &status, 0);
        child_exit_status[i] = WEXITSTATUS(status);
        if (child_exit_status[i] == 0) {
	     child_found = 1;
	     break;
        }
    }

    if (!child_found) {
    	printf("No solution exists \n");
    }
    
    return 0;
}
