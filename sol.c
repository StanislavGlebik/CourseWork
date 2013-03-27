#include <stdio.h>
#include <Windows.h>
#define N 63
#define ALIGNMENT 64
#define NULL_DELETED NULL
#define NULL_NOT_CREATED 1
#define UNCHAINED ((LONG **)1)
#define EXTENDED_DEBUGS

typedef struct _vector
{
  volatile LONG64 ** array;
	volatile LONG64 data_size;
	volatile LONG64 array_size;
	volatile LONG used;
	volatile BOOL copied;
} *vector;

vector vectorStructures[N];
volatile LONG L, R, freedL;
volatile LONG64 data_size;

LONG64 ** createArray(LONG64 size) {
    int i;
	LONG64** result;

	printf("Size: %I64d\n", size);
	result = (LONG64**)_aligned_malloc(size * sizeof(LONG64*), ALIGNMENT);
	
    for (i = 0; i < size; ++i) {
		result[i] = NULL;
	}

    return result;
}

void initialization() {
    int i;
	for (i = 0; i < N; ++i) {
		vectorStructures[i] = (vector) _aligned_malloc(sizeof(struct _vector), ALIGNMENT);
        vectorStructures[i]->array = NULL;
        if (!i) {
            vectorStructures[i]->data_size = 0;
        }
        else {
            vectorStructures[i]->data_size = (LONG64)1 << (i-1);
        }
        vectorStructures[i]->array_size = (LONG64)1 << i;
        vectorStructures[i]->copied = 0;
        vectorStructures[i]->used = 0;
    }
}

// Blocking
void freeVectorAllVectorStructures(vector v) {
	//TODO
}

void freeArray(LONG64** array) {
    LONG64 i;

    _aligned_free(array);
    array = NULL;
}


// -------------------------Box operations--------------------------
BOOL ifBoxed(volatile LONG64 * pointer) {
	return (LONG64)pointer & 1;
}

volatile LONG64* makeItBoxed(volatile LONG64* pointer) {
	return (volatile LONG64*)((LONG)pointer | 1);
}

volatile LONG64* makeItUnboxed(volatile LONG64* pointer) {
	return (volatile LONG64 *)(((LONG) pointer >> 1) << 1);
}
//------------------------------------------------------------------

DWORD WINAPI copyThread(PVOID _num) {
	int num = (int) _num;
	int i;

	printf("Copy thread\n");
    if (num < 0) {
        return 0;
    }

	for (i = 0; i < vectorStructures[num]->array_size; ++i) {
		volatile LONG64 * pointer, *boxedPointer, *newPointer;
		int cnt = 1;

		pointer = vectorStructures[num]->array[i];
        boxedPointer = makeItBoxed(pointer);
		while ((newPointer = (LONG64 *)InterlockedCompareExchangePointer((PVOID *)&vectorStructures[num]->array[i], (PVOID)boxedPointer, (PVOID)pointer))
			!=  pointer) {
			pointer = newPointer;
			boxedPointer = makeItBoxed(pointer);
		}

		if (pointer == NULL) {
			continue;
		}

		while ((newPointer = (LONG64 *) InterlockedCompareExchangePointer((PVOID *)&vectorStructures[num+cnt]->array[i], (PVOID)pointer, NULL))
			!= NULL) {
				if (!ifBoxed(newPointer)) {
					break;
				}
				else {
					if (makeItUnboxed(newPointer) == NULL) {
                        ++cnt;
					}
					else {
						break;
					}
				}
		}
	}
    InterlockedIncrement(&vectorStructures[num]->copied);
	return 0;
}

void freeNoMoreUsedStructures() {
    int curFreedL;
    printf("Free no more used structures\n");
    while (TRUE) {
        curFreedL = freedL;

        if (!vectorStructures[curFreedL]->copied) {
            return;
        }

        if (InterlockedCompareExchange(&vectorStructures[curFreedL]->used, -1, 0) != 0) {
            return;
        }
        
        printf("Freeing...\n");
        freeArray(vectorStructures[curFreedL]->array, vectorStructures[curFreedL]->array_size);
        
        // Not necessary, for debug purposes
        vectorStructures[curFreedL]->array = NULL;
        InterlockedIncrement(&freedL);
    }
}

void push_back(LONG64 _newElement) {
	int curR;
	LONG64** newArray;
	LONG64 curSize;
	LONG64 newSize;
	volatile LONG64 * pointerToReplace = NULL, *newPointer;
	LONG myUsed;
	LONG newMyUsed;
	LONG64* newElement = (LONG64*) _aligned_malloc(sizeof(LONG64), ALIGNMENT);

	*newElement = _newElement;
    freeNoMoreUsedStructures();

start:

	curR = R;
	myUsed = vectorStructures[curR]->used;
	if (myUsed == -1) {
		goto start;
	}

    // Ensure that nobody has deleted or in process of deleting curR structure
	while ((newMyUsed = InterlockedCompareExchange(&vectorStructures[curR]->used, myUsed+1, myUsed)) != myUsed) {
		if (newMyUsed == -1) {
			goto start;
		}
		myUsed = newMyUsed;
	}

    // Nobody has created new, bigger array. Let's do it!
	if (vectorStructures[curR]->array == NULL) {
		newArray = createArray((LONG64)1 << curR);
        // Perfomance problem
        if (InterlockedCompareExchangePointer((PVOID *)&vectorStructures[curR]->array, newArray, NULL) != NULL) {
			freeArray(newArray);
		}
		else {
			DWORD threadIDForNothing = 0;
			HANDLE h = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)copyThread, (PVOID) (curR-1), 0, &threadIDForNothing);
			printf("Let's copy! %d data size %d\n", (1 << curR), vectorStructures[curR]->data_size);
			if (h == NULL) {
                printf("Error: %d", GetLastError());
            }
		}
	}


	curSize = data_size;
	//newSize = 0;

	if (curSize >= vectorStructures[curR]->array_size) {
		InterlockedDecrement(&vectorStructures[curR]->used);		
		goto start;
	}

    /*while ((newSize = InterlockedCompareExchange64(&data_size, curSize+1, curSize)) != curSize) {
		curSize = newSize;
		if (curSize >= vectorStructures[curR]->array_size) {
			break;
		}
	}*/
	
	//curSize = newSize;

	//Problems: increase size, but write nothing (pointer is empty). Idea: don't change size, insert pointers. Another thread: If pointer is not NULL increase size.
	/*if (curSize >= vectorStructures[curR]->array_size) {
		InterlockedCompareExchange(&R, curR + 1, curR);
		InterlockedDecrement(&vectorStructures[curR]->used);
		goto start;
	}*/

	while ((newPointer = (LONG64 *)InterlockedCompareExchangePointer((PVOID*)&vectorStructures[curR]->array[curSize], newElement, (PVOID)pointerToReplace)) != pointerToReplace) {
		if (ifBoxed(newPointer)) {
			++curR;
			InterlockedIncrement(&vectorStructures[curR]->used);
			InterlockedDecrement(&vectorStructures[curR-1]->used);
			pointerToReplace = NULL;
		}
		else if (newPointer != NULL) {
            InterlockedCompareExchange64(&data_size, curSize+1, curSize);
            curSize = data_size;
            pointerToReplace = NULL;
	        if (curSize >= vectorStructures[curR]->array_size) {
		        InterlockedCompareExchange(&R, curR + 1, curR);
		        InterlockedDecrement(&vectorStructures[curR]->used);
		        goto start;
        	}
        }
        else {
			pointerToReplace = newPointer;
		}
	}

	InterlockedDecrement(&vectorStructures[curR]->used);
}

LONG64 elementAt(LONG64 index) {
	//TODO
}

void DEBUG_printAllInformation() {
    int i;

#ifdef EXTENDED_DEBUGS
	int j;
	int k;
#endif

    printf("L: %d\n", L);
    printf("R: %d\n", R);
    printf("freedL: %d\n", freedL);    
    printf("Data size: %d\n", data_size);
    printf("Vector structures:\n");
    for (i = 0; i < N; ++i) {
        printf("\t#%d\n", i);
        if (vectorStructures[i]->array == NULL || makeItUnboxed(vectorStructures[i]->array) == NULL) {
            printf("\t\tArray is NULL\n");
        }
        else  {
            printf("\t\tArray is not NULL\n");            
#ifdef EXTENDED_DEBUGS
			k = vectorStructures[i]->array_size;

			for (j = 0; j < k; ++j) {
				if (ifBoxed(vectorStructures[i]->array[j])) {
					if (makeItUnboxed(vectorStructures[i]->array[j]) != NULL) {
                        printf("\t\t\t%I64d - boxed\n", *makeItUnboxed(vectorStructures[i]->array[j]));
                    }
                    else {
                        printf("\t\t\tNULL - boxed\n");
                    }
				}
				else {
                    if (vectorStructures[i]->array[j] != NULL) {
					    printf("\t\t\t%I64d\n", *vectorStructures[i]->array[j]);					
                    }
                    else {
                        printf("\t\t\tNULL\n");
                    }
				}
			}
#endif
		}

        printf("\t\tData size: %I64d\n", vectorStructures[i]->data_size);
        printf("\t\tArray size: %I64d\n", vectorStructures[i]->array_size);
        printf("\t\tUsed: %d\n", vectorStructures[i]->used);
        printf("\t\tCopied: %d\n", vectorStructures[i]->copied);
    }
}

DWORD WINAPI testThread(PVOID _num) {
	LONG64 num = (LONG64) _num;
	LONG64 i;
	LONG64 cnt;

	if (num == 4) {
		cnt = 40;
	}
	else {      
		cnt = 1;
	}
	for (i =  num; i < num + cnt; ++i) {
		Sleep(10);
		push_back(i);
	}

	return 0;
}

int main() {
    DWORD i;
	DWORD threadID = 0;
#ifdef _DEBUG
    freopen("output.txt", "w", stdout);
#endif

	initialization();

	CreateThread(NULL, NULL, testThread, (LPVOID) 0, 0, &threadID);
	CreateThread(NULL, NULL, testThread, (LPVOID) 4, 0, &threadID);
	//CreateThread(NULL, NULL, testThread, (LPVOID) 5, 0, &threadID);

	Sleep(1000);
	
	threadID = threadID;

#ifdef _DEBUG
    DEBUG_printAllInformation();
#endif    
    return 0;
}
