#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include "traffic.h"

extern struct intersection isection;

/**
 * Populate the car lists by parsing a file where each line has
 * the following structure:
 *
 * <id> <in_direction> <out_direction>
 *
 * Each car is added to the list that corresponds with
 * its in_direction
 *
 * Note: this also updates 'inc' on each of the lanes
 */
void parse_schedule(char *file_name) {
    int id;
    struct car *cur_car;
    struct lane *cur_lane;
    enum direction in_dir, out_dir;
    FILE *f = fopen(file_name, "r");

    /* parse file */
    while (fscanf(f, "%d %d %d", &id, (int*)&in_dir, (int*)&out_dir) == 3) {

        /* construct car */
        cur_car = malloc(sizeof(struct car));
        cur_car->id = id;
        cur_car->in_dir = in_dir;
        cur_car->out_dir = out_dir;

        /* append new car to head of corresponding list */
        cur_lane = &isection.lanes[in_dir];
        cur_car->next = cur_lane->in_cars;
        cur_lane->in_cars = cur_car;
        cur_lane->inc++;
    }

    fclose(f);
}

/**
 * TODO: Fill in this function
 *
 * Do all of the work required to prepare the intersection
 * before any cars start coming
 *
 */
void init_intersection() {
	int i;
	for(i = 0; i < 4; i++){
		// Initializing quadrant mutex locks
		pthread_mutex_init(&isection.quad[i], NULL);
		// Initializing lane locks
        pthread_mutex_init(&isection.lanes[i].lock, NULL);
		// Initializing conditions for lanes
		pthread_cond_init(&isection.lanes[i].producer_cv, NULL);
        pthread_cond_init(&isection.lanes[i].consumer_cv, NULL);
		// Allocate buffer memory
		isection.lanes[i].buffer = calloc(LANE_LENGTH, sizeof(struct car));
		if (isection.lanes[i].buffer == NULL) { // Catch memory alloc error
			perror("calloc");
			exit(1);
		}

		// Counters
		isection.lanes[i].passed = 0;
		isection.lanes[i].tail = 0;
		isection.lanes[i].in_buf = 0;
		isection.lanes[i].head = 0;
		// Constants
		isection.lanes[i].capacity = LANE_LENGTH;
	}
}

/**
 * TODO: Fill in this function
 *
 * Populates the corresponding lane with cars as room becomes
 * available. Ensure to notify the cross thread as new cars are
 * added to the lane.
 *
 */
void *car_arrive(void *arg) {
    struct lane *l = arg; // Getting the lane
    pthread_mutex_lock(&(l->lock)); // Obtaining lane lock
	struct car *curcar = l->in_cars; // Grabbing first car in pending list

    pthread_mutex_unlock(&(l->lock));
	while (curcar != NULL) { // Loop until no more cars pending entry
        pthread_mutex_lock(&(l->lock)); // Obtaining lane lock
	    while(l->in_buf >= l->capacity) { // If full, wait.
	        pthread_cond_wait(&(l->producer_cv), &(l->lock));
	    }

	    l->buffer[l->tail] = curcar;
	    l->in_buf++;
	    l->tail++;
		l->tail = l->tail % l->capacity; // Indexes should stay within range of 0-capacity

		// Send signal that a car is available to cross
	    pthread_cond_signal(&(l->consumer_cv));
		// Get next car waiting to enter lane
		curcar = curcar->next;
        pthread_mutex_unlock(&(l->lock));
	}
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Moves cars from a single lane across the intersection. Cars
 * crossing the intersection must abide the rules of the road
 * and cross along the correct path. Ensure to notify the
 * arrival thread as room becomes available in the lane.
 *
 * Note: After crossing the intersection the car should be added
 * to the out_cars list of the lane that corresponds to the car's
 * out_dir. Do not free the cars!
 *
 *
 * Note: For testing purposes, each car which gets to cross the
 * intersection should print the following three numbers on a
 * new line, separated by spaces:
 *  - the car's 'in' direction, 'out' direction, and id.
 *
 * You may add other print statements, but in the end, please
 * make sure to clear any prints other than the one specified above,
 * before submitting your final code.
 */
void *car_cross(void *arg) {
    struct lane *l = arg; // Getting the lane
	pthread_mutex_lock(&(l->lock)); // Obtaining lane lock
    struct car *cars;
    
    while(l->inc != l->passed){
		//wait until there is something in the buffer.
	    while(l->in_buf == 0){
    	    pthread_cond_wait(&(l->consumer_cv), &(l->lock));
	    }

        cars = l->buffer[l->head];
        int *path = compute_path(cars->in_dir, cars->out_dir);

        // Attempt to acquire quadrant locks
        int i;
		for (i = 0; i < MAX_DIRECTION; i++) {
			if (path[i] <= 0) { // No more quadrants required
				break;
			}
			pthread_mutex_lock(&(isection.quad[path[i] - 1]));
		}

        // Send cars out
        cars->next = l->out_cars;
        l->out_cars = cars;
        fprintf(stdout, "%d %d %d\n", cars->in_dir, cars->out_dir, cars->id);

        l->passed++;
        l->head++;
		l->head = l->head % l->capacity; // Indexes should stay within range of 0-capacity
        l->in_buf--;

        // Unlock the quadrants we used
		for (i = 0; i < MAX_DIRECTION; i++) {
			if (path[i] <= 0) { // No more quadrants required
				break;
			}
			pthread_mutex_unlock(&(isection.quad[path[i] - 1]));
		}

        //send the signal there is space avail in buffer
        pthread_cond_signal(&(l->producer_cv));
		// Free path as it was malloc'd
		free(path);
    }

    pthread_mutex_unlock(&(l->lock));
    return NULL;
}

/**
 * TODO: Fill in this function
 *
 * Given a car's in_dir and out_dir return a sorted
 * list of the quadrants the car will pass through.
 *
 */
int *compute_path(enum direction in_dir, enum direction out_dir) {
    int *result = (int*) calloc(MAX_DIRECTION, sizeof(int));
	if (result == NULL) {
		perror("malloc");
		exit(1);
	}

    if(in_dir == NORTH){
	    if(out_dir == SOUTH){ // Straight
			result[0] = 2;
			result[1] = 3;	
		} else if(out_dir == EAST){ // Left
			result[0] = 2;
			result[1] = 3;
			result[2] = 4;		
		} else if(out_dir == WEST){ // Right
			result[0] = 2;
		} else if(out_dir == NORTH){ // U-turn
			result[0] = 1;
			result[1] = 2;	
		}
	} else if(in_dir == SOUTH){
		if(out_dir == NORTH){ // Straight
			result[0] = 1;
			result[1] = 4;
		} else if(out_dir == WEST){ // Left
			result[0] = 1;
			result[1] = 2;
			result[2] = 4;
		} else if(out_dir == EAST){ // Right
			result[0] = 4;		
		} else if(out_dir == SOUTH){ // U-turn
			result[0] = 3;
			result[1] = 4;			
		}
	} else if(in_dir == WEST){
		if(out_dir == EAST){
			result[0] = 3;
			result[1] = 4;
		} else if(out_dir == NORTH){
			result[0] = 1;
			result[1] = 3;
			result[2] = 4;
		} else if(out_dir == SOUTH){
			result[0] = 3;
		} else if(out_dir == WEST){
			result[0] = 2;
			result[1] = 3;
		}
	} else if(in_dir == EAST){
		if(out_dir == WEST){
			result[0] = 1;
			result[1] = 2;
		} else if(out_dir == SOUTH){
			result[0] = 1;
			result[1] = 2;
			result[2] = 3;
		} else if(out_dir == NORTH){
			result[0] = 1;
		} else if(out_dir == EAST){
			result[0] = 1;
			result[1] = 4;
		}
	}
    return result;
}
