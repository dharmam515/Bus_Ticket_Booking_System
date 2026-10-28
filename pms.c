#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEFAULT_SEATS_PER_BUS 10
#define MAX_SEATS_PER_BUS 60
#define NUM_BUS_TYPES 3
#define MAX_BUSES_PER_TYPE 20
#define TABLE_SIZE 101

/* Bus Type Constants */
const char *BUS_TYPE_NAMES[NUM_BUS_TYPES] = {
    "Deluxe",
    "Ultra Deluxe (Without AC)",
    "Ultra Deluxe (With AC)"
};

const double BUS_TYPE_FARES[NUM_BUS_TYPES] = {
    250.0,
    350.0,
    500.0
};

/* Individual passenger details */
typedef struct Passenger
{
    char name[50];
    char gender;          /* 'M' or 'F' */
    int age;
    char phone[20];
    int seat;             /* 0 if waiting, 1..totalSeats if confirmed */
    int exceptionMark;    /* 1 if booked under opposite-gender override / last-seat exception */
} Passenger;

/* Booking node (individual or group reservation) */
typedef struct Booking
{
    int id;
    int busType;          /* 0: Deluxe, 1: Ultra Deluxe (No AC), 2: Ultra Deluxe (AC) */
    int busIndex;         /* 0-based index of bus within busType */
    int count;            /* Number of active passengers */
    int isGroup;          /* 1 if count >= 2, 0 otherwise */
    double totalFare;
    int status;           /* 1 = Confirmed, 0 = Waiting */
    Passenger *passengers;
    struct Booking *next;
} Booking;

/* Queue node for waiting list */
typedef struct QNode
{
    Booking *booking;
    struct QNode *next;
} QNode;

/* Hash table node for O(1) booking lookup */
typedef struct HashEntry
{
    int id;
    Booking *booking;
    struct HashEntry *next;
} HashEntry;

/* Seat state on a specific bus */
typedef struct SeatInfo
{
    int occupied;         /* 0 = Empty, 1 = Occupied */
    int bookingId;
    char passengerName[50];
    char gender;          /* 'M' or 'F' */
    int exceptionMark;    /* 1 = Opposite-gender adjacent / Last-seat marked */
} SeatInfo;

/* Individual Bus structure */
typedef struct Bus
{
    int busType;
    int busNumber;        /* 1-based number within its type */
    int totalSeats;       /* Configurable seat capacity for this bus */
    SeatInfo seats[MAX_SEATS_PER_BUS + 1]; /* 1-indexed seats */
} Bus;

/* Default 5 buses across 3 types: 2 Deluxe, 2 Ultra Non-AC, 1 Ultra AC */
int totalBusesOfType[NUM_BUS_TYPES] = {2, 2, 1};
int seatsPerBusType[NUM_BUS_TYPES] = {
    DEFAULT_SEATS_PER_BUS,
    DEFAULT_SEATS_PER_BUS,
    DEFAULT_SEATS_PER_BUS
};
Bus fleet[NUM_BUS_TYPES][MAX_BUSES_PER_TYPE];

/* Linked list for confirmed bookings */
Booking *confirmedHead = NULL;

/* Queue for waiting list bookings */
QNode *front = NULL;
QNode *rear = NULL;

/* Hash table for booking lookup */
HashEntry *hashTable[TABLE_SIZE];

int nextBookingId = 1001;

/* ---------------- TERMINAL UI & INPUT VALIDATION HELPERS ---------------- */

void clearScreen(void)
{
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

int strIgnoreCaseCmp(const char *s1, const char *s2)
{
    while (*s1 && *s2)
    {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2)
            return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

void printAppBanner(const char *subtitle)
{
    printf("========================================================================\n");
    printf("              PUBLIC TRANSPORT BUS RESERVATION SYSTEM                   \n");
    if (subtitle != NULL && strlen(subtitle) > 0)
        printf("  >> %s\n", subtitle);
    printf("========================================================================\n");
}

int readInt(const char *prompt)
{
    char line[100];
    char *end;
    long value;

    while (1)
    {
        printf("%s", prompt);
        if (fgets(line, sizeof(line), stdin) == NULL)
            exit(0);

        value = strtol(line, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')
            end++;

        if (end != line && *end == '\0')
            return (int)value;

        printf("  [!] invalid choice! try again\n");
    }
}

void readString(const char *prompt, char *buffer, int size)
{
    while (1)
    {
        printf("%s", prompt);
        if (fgets(buffer, size, stdin) == NULL)
            exit(0);

        buffer[strcspn(buffer, "\r\n")] = '\0';

        char *start = buffer;
        while (*start == ' ' || *start == '\t')
            start++;

        if (start != buffer)
            memmove(buffer, start, strlen(start) + 1);

        int len = (int)strlen(buffer);
        while (len > 0 && (buffer[len - 1] == ' ' || buffer[len - 1] == '\t'))
            buffer[--len] = '\0';

        if (len > 0)
            return;

        printf("  [!] Input cannot be empty. Try again.\n");
    }
}

/* Validates that Name is a proper alphabetical string (letters and spaces only) */
void readName(const char *prompt, char *buffer, int size)
{
    while (1)
    {
        readString(prompt, buffer, size);

        int valid = 1;
        int hasAlpha = 0;
        for (int i = 0; buffer[i] != '\0'; i++)
        {
            unsigned char ch = (unsigned char)buffer[i];
            if (isalpha(ch))
            {
                hasAlpha = 1;
            }
            else if (ch != ' ')
            {
                valid = 0;
                break;
            }
        }

        if (valid && hasAlpha)
            return;

        printf("  [!] Invalid name! Name must be a proper alphabetical string. Try again.\n");
    }
}

/* Validates that Age is strictly between 0 and 100 (1..100) */
int readAge(const char *prompt)
{
    while (1)
    {
        int age = readInt(prompt);
        if (age > 0 && age <= 100)
            return age;

        printf("  [!] Invalid age! Age must be between 0 to 100. Try again.\n");
    }
}

/* Validates that Phone Number is exactly 10 digits */
void readPhone(const char *prompt, char *buffer, int size)
{
    char temp[100];
    while (1)
    {
        readString(prompt, temp, sizeof(temp));

        int len = (int)strlen(temp);
        int allDigits = 1;
        for (int i = 0; i < len; i++)
        {
            if (!isdigit((unsigned char)temp[i]))
            {
                allDigits = 0;
                break;
            }
        }

        if (len == 10 && allDigits)
        {
            strncpy(buffer, temp, size - 1);
            buffer[size - 1] = '\0';
            return;
        }

        printf("  [!] Invalid phone number! Phone number must be exactly 10 digits. Try again.\n");
    }
}

char readGender(const char *prompt)
{
    char buf[20];
    while (1)
    {
        readString(prompt, buf, sizeof(buf));
        if (strlen(buf) == 1)
        {
            char g = (char)toupper((unsigned char)buf[0]);
            if (g == 'M' || g == 'F')
                return g;
        }
        printf("  [!] Invalid gender! Enter 'M' for Male or 'F' for Female.\n");
    }
}

/* ---------------- INITIALIZATION & FLEET HELPERS ---------------- */

void initFleet(void)
{
    for (int t = 0; t < NUM_BUS_TYPES; t++)
    {
        for (int b = 0; b < MAX_BUSES_PER_TYPE; b++)
        {
            fleet[t][b].busType = t;
            fleet[t][b].busNumber = b + 1;
            fleet[t][b].totalSeats = seatsPerBusType[t];
            for (int s = 1; s <= MAX_SEATS_PER_BUS; s++)
            {
                fleet[t][b].seats[s].occupied = 0;
                fleet[t][b].seats[s].bookingId = 0;
                fleet[t][b].seats[s].passengerName[0] = '\0';
                fleet[t][b].seats[s].gender = '\0';
                fleet[t][b].seats[s].exceptionMark = 0;
            }
        }
    }
}

int getAvailableSeatsInBus(int busType, int busIndex)
{
    int avail = 0;
    int cap = fleet[busType][busIndex].totalSeats;
    for (int s = 1; s <= cap; s++)
    {
        if (!fleet[busType][busIndex].seats[s].occupied)
            avail++;
    }
    return avail;
}

int getAvailableBusCount(int busType)
{
    int count = 0;
    for (int b = 0; b < totalBusesOfType[busType]; b++)
    {
        if (getAvailableSeatsInBus(busType, b) >= 1)
            count++;
    }
    return count;
}

int getTotalAvailableSeatsInType(int busType)
{
    int total = 0;
    for (int b = 0; b < totalBusesOfType[busType]; b++)
    {
        total += getAvailableSeatsInBus(busType, b);
    }
    return total;
}

void printBusSeatLayout(int busType, int busIndex)
{
    Bus *bus = &fleet[busType][busIndex];
    int avail = getAvailableSeatsInBus(busType, busIndex);

    printf("\n+----------------------------------------------------------------------+\n");
    printf("| LIVE SEAT MAP: %-28s | Bus #%-2d (Avail: %2d/%2d) |\n",
           BUS_TYPE_NAMES[busType], busIndex + 1, avail, bus->totalSeats);
    printf("+----------------------------------------------------------------------+\n");

    for (int s = 1; s <= bus->totalSeats; s++)
    {
        char cell[32];
        if (!bus->seats[s].occupied)
        {
            snprintf(cell, sizeof(cell), "[Seat %02d: FREE        ]", s);
        }
        else
        {
            char shortName[10];
            strncpy(shortName, bus->seats[s].passengerName, 9);
            shortName[9] = '\0';
            snprintf(cell, sizeof(cell), "[Seat %02d: %-9s(%c)]",
                     s, shortName, bus->seats[s].gender);
        }

        printf("  %-30s", cell);
        if (s % 2 == 0 || s == bus->totalSeats)
            printf("\n");
    }
    printf("+----------------------------------------------------------------------+\n");
}

/* ---------------- HASH TABLE, LINKED LIST & QUEUE ---------------- */

int hashFunction(int id)
{
    return id % TABLE_SIZE;
}

Booking *searchBooking(int id)
{
    int index = hashFunction(id);
    HashEntry *temp = hashTable[index];

    while (temp != NULL)
    {
        if (temp->id == id)
            return temp->booking;
        temp = temp->next;
    }
    return NULL;
}

void insertHash(Booking *b)
{
    int index = hashFunction(b->id);
    HashEntry *entry = malloc(sizeof(HashEntry));
    if (entry == NULL)
    {
        printf("Memory allocation failed.\n");
        exit(1);
    }
    entry->id = b->id;
    entry->booking = b;
    entry->next = hashTable[index];
    hashTable[index] = entry;
}

void deleteHash(int id)
{
    int index = hashFunction(id);
    HashEntry *temp = hashTable[index];
    HashEntry *prev = NULL;

    while (temp != NULL)
    {
        if (temp->id == id)
        {
            if (prev == NULL)
                hashTable[index] = temp->next;
            else
                prev->next = temp->next;
            free(temp);
            return;
        }
        prev = temp;
        temp = temp->next;
    }
}

void addConfirmed(Booking *b)
{
    b->status = 1;
    b->next = confirmedHead;
    confirmedHead = b;
}

void removeConfirmed(int id)
{
    Booking *temp = confirmedHead;
    Booking *prev = NULL;

    while (temp != NULL)
    {
        if (temp->id == id)
        {
            if (prev == NULL)
                confirmedHead = temp->next;
            else
                prev->next = temp->next;
            temp->next = NULL;
            return;
        }
        prev = temp;
        temp = temp->next;
    }
}

void enqueueWaiting(Booking *b)
{
    b->status = 0;
    QNode *node = malloc(sizeof(QNode));
    if (node == NULL)
    {
        printf("Memory allocation failed.\n");
        exit(1);
    }
    node->booking = b;
    node->next = NULL;

    if (rear == NULL)
        front = rear = node;
    else
    {
        rear->next = node;
        rear = node;
    }
}

int removeWaitingByNode(QNode *target)
{
    QNode *temp = front;
    QNode *prev = NULL;

    while (temp != NULL)
    {
        if (temp == target)
        {
            if (prev == NULL)
                front = temp->next;
            else
                prev->next = temp->next;

            if (temp == rear)
                rear = prev;

            free(temp);
            return 1;
        }
        prev = temp;
        temp = temp->next;
    }
    return 0;
}

int removeWaitingById(int id)
{
    QNode *temp = front;
    while (temp != NULL)
    {
        if (temp->booking->id == id)
            return removeWaitingByNode(temp);
        temp = temp->next;
    }
    return 0;
}

/* ---------------- GENDER & ADJACENCY HELPERS ---------------- */

int findOppositeGenderNeighbor(Bus *bus, int seat, char myGender, int myBookingId,
                               int *outAdjSeat, char *outAdjName, char *outAdjGender)
{
    int neighbors[2] = {seat - 1, seat + 1};

    for (int i = 0; i < 2; i++)
    {
        int adj = neighbors[i];
        if (adj >= 1 && adj <= bus->totalSeats && bus->seats[adj].occupied)
        {
            if (myBookingId != 0 && bus->seats[adj].bookingId == myBookingId)
                continue;

            if (bus->seats[adj].gender != myGender)
            {
                if (outAdjSeat)
                    *outAdjSeat = adj;
                if (outAdjName)
                    strcpy(outAdjName, bus->seats[adj].passengerName);
                if (outAdjGender)
                    *outAdjGender = bus->seats[adj].gender;
                return 1;
            }
        }
    }
    return 0;
}

int isSeatCompatibleForGender(Bus *bus, int s, char g)
{
    return !findOppositeGenderNeighbor(bus, s, g, 0, NULL, NULL, NULL);
}

void assignSeatToPassenger(Bus *bus, int bookingId, Passenger *p, int s, int markException)
{
    bus->seats[s].occupied = 1;
    bus->seats[s].bookingId = bookingId;
    strcpy(bus->seats[s].passengerName, p->name);
    bus->seats[s].gender = p->gender;
    bus->seats[s].exceptionMark = markException;

    p->seat = s;
    p->exceptionMark = markException;
}

void interactiveSeatAllocation(Bus *bus, Booking *b)
{
    for (int i = 0; i < b->count; i++)
    {
        Passenger *p = &b->passengers[i];
        int availNow = getAvailableSeatsInBus(bus->busType, bus->busNumber - 1);

        printBusSeatLayout(bus->busType, bus->busNumber - 1);
        printf("\n Seat Selection for Passenger %d of %d: %s (Gender: %c, Age: %d)\n",
               i + 1, b->count, p->name, p->gender, p->age);

        int chosenSeat = 0;
        while (1)
        {
            char promptBuf[80];
            snprintf(promptBuf, sizeof(promptBuf),
                     "  Enter preferred Seat Number (1-%d): ", bus->totalSeats);
            chosenSeat = readInt(promptBuf);

            if (chosenSeat < 1 || chosenSeat > bus->totalSeats)
            {
                printf("  invalid choice! try again\n");
                continue;
            }
            if (bus->seats[chosenSeat].occupied)
            {
                printf("  [!] Seat %d is already occupied by %s (%c). Please choose an empty seat.\n",
                       chosenSeat, bus->seats[chosenSeat].passengerName, bus->seats[chosenSeat].gender);
                continue;
            }
            break;
        }

        int adjSeat = 0;
        char adjName[50] = "";
        char adjGender = '\0';

        int hasConflict = findOppositeGenderNeighbor(
            bus, chosenSeat, p->gender, b->isGroup ? b->id : 0,
            &adjSeat, adjName, &adjGender);

        if (!hasConflict)
        {
            assignSeatToPassenger(bus, b->id, p, chosenSeat, 0);
            printf("  [OK] Seat %d allocated to %s.\n", chosenSeat, p->name);
        }
        else
        {
            printf("\n  +--------------------------------------------------------------------+\n");
            printf("  | [!] GENDER ADJACENCY SUGGESTION                                    |\n");
            printf("  +--------------------------------------------------------------------+\n");
            printf("  Are you sure to book here because %s (%s) reserved the adjacent seat (Seat %d)?\n",
                   (adjGender == 'M' ? "Male" : "Female"), adjName, adjSeat);
            printf("    1. OK (Book Seat %d)\n", chosenSeat);
            printf("    2. No (Automatically allocate another seat)\n");

            int decision = 0;
            while (1)
            {
                decision = readInt("  Enter choice (1 = OK / 2 = No): ");
                if (decision == 1 || decision == 2)
                    break;
                printf("  invalid choice! try again\n");
            }

            if (decision == 1)
            {
                assignSeatToPassenger(bus, b->id, p, chosenSeat, 1);
                printf("  [Confirmed] Seat %d booked for %s.\n", chosenSeat, p->name);
            }
            else
            {
                if (availNow == 1)
                {
                    assignSeatToPassenger(bus, b->id, p, chosenSeat, 1);
                    printf("  [Last Seat] Seat %d is the last remaining seat; allocated Seat %d to %s.\n",
                           chosenSeat, chosenSeat, p->name);
                }
                else
                {
                    int autoSeat = 0;
                    for (int s = 1; s <= bus->totalSeats; s++)
                    {
                        if (!bus->seats[s].occupied && s != chosenSeat)
                        {
                            if (!findOppositeGenderNeighbor(bus, s, p->gender,
                                                            b->isGroup ? b->id : 0,
                                                            NULL, NULL, NULL))
                            {
                                autoSeat = s;
                                break;
                            }
                        }
                    }

                    if (autoSeat != 0)
                    {
                        assignSeatToPassenger(bus, b->id, p, autoSeat, 0);
                        printf("  [Auto-Allocated] Seat %d automatically allocated to %s.\n",
                               autoSeat, p->name);
                    }
                    else
                    {
                        for (int s = 1; s <= bus->totalSeats; s++)
                        {
                            if (!bus->seats[s].occupied && s != chosenSeat)
                            {
                                autoSeat = s;
                                break;
                            }
                        }
                        if (autoSeat == 0)
                            autoSeat = chosenSeat;

                        assignSeatToPassenger(bus, b->id, p, autoSeat, 1);
                        printf("  [Auto-Allocated] Seat %d automatically allocated to %s (Marked).\n",
                               autoSeat, p->name);
                    }
                }
            }
        }
    }
}

int autoAllocateWaitingBooking(Bus *bus, Booking *b)
{
    int avail = getAvailableSeatsInBus(bus->busType, bus->busNumber - 1);
    if (avail < b->count)
        return 0;

    if (b->count == 1)
    {
        char g = b->passengers[0].gender;
        for (int s = 1; s <= bus->totalSeats; s++)
        {
            if (!bus->seats[s].occupied && isSeatCompatibleForGender(bus, s, g))
            {
                assignSeatToPassenger(bus, b->id, &b->passengers[0], s, 0);
                return 1;
            }
        }

        if (avail == 1)
        {
            for (int s = 1; s <= bus->totalSeats; s++)
            {
                if (!bus->seats[s].occupied)
                {
                    assignSeatToPassenger(bus, b->id, &b->passengers[0], s, 1);
                    return 1;
                }
            }
        }
        return 0;
    }

    int k = b->count;
    int assignedSeats[MAX_SEATS_PER_BUS + 1];
    int usedPassenger[MAX_SEATS_PER_BUS + 1];
    memset(usedPassenger, 0, sizeof(usedPassenger));
    int foundCount = 0;

    for (int s = 1; s <= bus->totalSeats && foundCount < k; s++)
    {
        if (!bus->seats[s].occupied)
            assignedSeats[foundCount++] = s;
    }

    if (foundCount == k)
    {
        for (int i = 0; i < k; i++)
        {
            int s = assignedSeats[i];
            int chosenP = -1;
            for (int p = 0; p < k; p++)
            {
                if (!usedPassenger[p] &&
                    !findOppositeGenderNeighbor(bus, s, b->passengers[p].gender, b->id, NULL, NULL, NULL))
                {
                    chosenP = p;
                    break;
                }
            }
            if (chosenP == -1)
            {
                for (int p = 0; p < k; p++)
                {
                    if (!usedPassenger[p])
                    {
                        chosenP = p;
                        break;
                    }
                }
            }
            usedPassenger[chosenP] = 1;
            assignSeatToPassenger(bus, b->id, &b->passengers[chosenP], s, 0);
        }
        return 1;
    }
    return 0;
}

/* ---------------- PASSENGER INPUT COLLECTION ---------------- */

void collectPassengers(Booking *b)
{
    for (int i = 0; i < b->count; i++)
    {
        printf("\n--- Enter Personal Details for Passenger %d of %d ---\n", i + 1, b->count);
        readName("  Enter passenger name: ", b->passengers[i].name, sizeof(b->passengers[i].name));
        b->passengers[i].gender = readGender("  Enter gender (M/F): ");
        b->passengers[i].age = readAge("  Enter age (1-100): ");
        readPhone("  Enter 10-digit phone number: ", b->passengers[i].phone, sizeof(b->passengers[i].phone));
        b->passengers[i].seat = 0;
        b->passengers[i].exceptionMark = 0;
    }
}

/* ---------------- BOOKING FUNCTION ---------------- */

void displayBusTypeMenu(void)
{
    printf("\n--- Available Bus Types ---\n");
    for (int t = 0; t < NUM_BUS_TYPES; t++)
    {
        int availBuses = getAvailableBusCount(t);
        printf("  %d. %s [Available Buses: %d/%d | Fare: Rs. %.2f/seat]\n",
               t + 1, BUS_TYPE_NAMES[t], availBuses,
               totalBusesOfType[t], BUS_TYPE_FARES[t]);
    }
}

void bookTicket(void)
{
    int selectedType = -1;
    int selectedBusIdx = 0;
    int count = 0;

    while (1)
    {
        clearScreen();
        printAppBanner("NEW TICKET RESERVATION");

        displayBusTypeMenu();
        int typeChoice = -1;
        while (1)
        {
            typeChoice = readInt("\nSelect Bus Type (1-3, or 0 to return to Main Menu): ");
            if (typeChoice == 0)
            {
                clearScreen();
                printAppBanner("Reservation Cancelled - Returned to Main Menu");
                return;
            }
            if (typeChoice >= 1 && typeChoice <= NUM_BUS_TYPES)
                break;
            printf("invalid choice! try again\n");
        }

        selectedType = typeChoice - 1;
        if (totalBusesOfType[selectedType] == 0)
        {
            printf("No buses available for %s.\n", BUS_TYPE_NAMES[selectedType]);
            continue;
        }

        clearScreen();
        printAppBanner("BUS SEAT AVAILABILITY CHECK");
        printf("\n--- Seat Availability for %s ---\n", BUS_TYPE_NAMES[selectedType]);

        int maxAvailOnSingleBus = -1;
        int bestBusIdx = 0;

        for (int b = 0; b < totalBusesOfType[selectedType]; b++)
        {
            int avail = getAvailableSeatsInBus(selectedType, b);
            printf("Bus #%d: %d seat(s) available (out of %d)\n",
                   b + 1, avail, fleet[selectedType][b].totalSeats);
            if (avail > maxAvailOnSingleBus)
            {
                maxAvailOnSingleBus = avail;
                bestBusIdx = b;
            }
        }
        printf("Total Available Seats in %s: %d\n",
               BUS_TYPE_NAMES[selectedType], getTotalAvailableSeatsInType(selectedType));

        if (totalBusesOfType[selectedType] > 1)
        {
            char prompt[100];
            snprintf(prompt, sizeof(prompt),
                     "Select Bus Number (1 to %d, or 0 for auto-select best bus): ",
                     totalBusesOfType[selectedType]);
            while (1)
            {
                int busChoice = readInt(prompt);
                if (busChoice == 0)
                {
                    selectedBusIdx = bestBusIdx;
                    break;
                }
                else if (busChoice >= 1 && busChoice <= totalBusesOfType[selectedType])
                {
                    selectedBusIdx = busChoice - 1;
                    break;
                }
                else
                {
                    printf("invalid choice! try again\n");
                }
            }
        }
        else
        {
            selectedBusIdx = 0;
        }

        int availInSelectedBus = getAvailableSeatsInBus(selectedType, selectedBusIdx);

        printf("Selected: %s - Bus #%d | Available Seats: %d\n",
               BUS_TYPE_NAMES[selectedType], selectedBusIdx + 1, availInSelectedBus);
        printBusSeatLayout(selectedType, selectedBusIdx);

        if (count <= 0)
        {
            while (1)
            {
                count = readInt("\nEnter count of people to reserve: ");
                if (count > 0)
                    break;
                printf("invalid choice! try again\n");
            }
        }

        if (count > availInSelectedBus)
        {
            printf("\n+----------------------------------------------------------------------+\n");
            printf("| [!] SEAT OVERFLOW PREFERENCE MENU                                    |\n");
            printf("+----------------------------------------------------------------------+\n");
            printf("Requested count (%d) exceeds available seats (%d) in %s (Bus #%d).\n",
                   count, availInSelectedBus, BUS_TYPE_NAMES[selectedType], selectedBusIdx + 1);
            printf("  1. Change the type of the bus (or pick another bus)\n");
            printf("  2. Add all %d passenger(s) to the Waiting List for this bus\n", count);
            if (availInSelectedBus > 0)
                printf("  3. Reduce passenger count to %d (book available seats now)\n", availInSelectedBus);
            printf("  0. Cancel and return to Main Menu\n");

            int pref = -1;
            while (1)
            {
                pref = readInt("Enter your preference: ");
                if (pref == 0 || pref == 1 || pref == 2 || (pref == 3 && availInSelectedBus > 0))
                    break;
                printf("invalid choice! try again\n");
            }

            if (pref == 1)
            {
                int changeCount = 0;
                while (1)
                {
                    changeCount = readInt("Keep passenger count as same? (1 = Yes, 2 = Enter new count): ");
                    if (changeCount == 1 || changeCount == 2)
                        break;
                    printf("invalid choice! try again\n");
                }
                if (changeCount == 2)
                    count = 0;
                continue;
            }
            else if (pref == 2)
            {
                Booking *wb = malloc(sizeof(Booking));
                wb->id = nextBookingId++;
                wb->busType = selectedType;
                wb->busIndex = selectedBusIdx;
                wb->count = count;
                wb->isGroup = (count >= 2) ? 1 : 0;
                wb->totalFare = count * BUS_TYPE_FARES[selectedType];
                wb->status = 0;
                wb->passengers = malloc(sizeof(Passenger) * count);
                wb->next = NULL;

                printf("\nTotal Payment for %d seat(s) in %s: Rs. %.2f\n",
                       count, BUS_TYPE_NAMES[selectedType], wb->totalFare);

                collectPassengers(wb);
                insertHash(wb);
                enqueueWaiting(wb);

                clearScreen();
                printAppBanner("WAITING LIST REGISTRATION OUTPUT");
                printf("Status     : Added to Waiting List\n");
                printf("Booking ID : %d\n", wb->id);
                printf("Bus        : %s (Bus #%d)\n", BUS_TYPE_NAMES[selectedType], selectedBusIdx + 1);
                printf("Category   : %s (%d passenger(s))\n", wb->isGroup ? "Group" : "Single", wb->count);
                printf("Total Fare : Rs. %.2f\n", wb->totalFare);
                for (int i = 0; i < wb->count; i++)
                {
                    printf("  -> %s (%c, Age %d, Phone: %s)\n",
                           wb->passengers[i].name, wb->passengers[i].gender,
                           wb->passengers[i].age, wb->passengers[i].phone);
                }
                return;
            }
            else if (pref == 3 && availInSelectedBus > 0)
            {
                count = availInSelectedBus;
                break;
            }
            else
            {
                clearScreen();
                printAppBanner("Reservation Cancelled");
                return;
            }
        }
        else
        {
            break;
        }
    }

    Booking *b = malloc(sizeof(Booking));
    b->id = nextBookingId++;
    b->busType = selectedType;
    b->busIndex = selectedBusIdx;
    b->count = count;
    b->isGroup = (count >= 2) ? 1 : 0;
    b->totalFare = count * BUS_TYPE_FARES[selectedType];
    b->status = 0;
    b->passengers = malloc(sizeof(Passenger) * count);
    b->next = NULL;

    printf("\n+----------------------------------------------------------------------+\n");
    printf("| PAYMENT SUMMARY                                                      |\n");
    printf("+----------------------------------------------------------------------+\n");
    printf("  Bus Type        : %s (Bus #%d)\n", BUS_TYPE_NAMES[selectedType], selectedBusIdx + 1);
    printf("  Passenger Count : %d (%s)\n", count, b->isGroup ? "Group Reservation" : "Single Reservation");
    printf("  Fare per Seat   : Rs. %.2f\n", BUS_TYPE_FARES[selectedType]);
    printf("  Total Payment   : Rs. %.2f\n", b->totalFare);
    printf("+----------------------------------------------------------------------+\n");

    collectPassengers(b);

    Bus *bus = &fleet[selectedType][selectedBusIdx];
    interactiveSeatAllocation(bus, b);

    insertHash(b);
    addConfirmed(b);

    clearScreen();
    printAppBanner("BOOKING CONFIRMED - TICKET RECEIPT");
    printf("Booking ID : %d\n", b->id);
    printf("Bus        : %s (Bus #%d)\n", BUS_TYPE_NAMES[b->busType], b->busIndex + 1);
    printf("Type       : %s (%d passenger(s))\n", b->isGroup ? "Group" : "Single", b->count);
    printf("Total Paid : Rs. %.2f\n", b->totalFare);
    printf("Passengers :\n");
    for (int i = 0; i < b->count; i++)
    {
        printf("  -> Seat %02d | Name: %-15s | Gender: %c | Age: %2d | Phone: %-12s %s\n",
               b->passengers[i].seat,
               b->passengers[i].name,
               b->passengers[i].gender,
               b->passengers[i].age,
               b->passengers[i].phone,
               b->passengers[i].exceptionMark ? "[MARKED: Opposite-Gender Adjacent / Last-Seat]" : "");
    }
    printBusSeatLayout(b->busType, b->busIndex);
}

/* ---------------- WAITING LIST PROMOTION ON CANCELLATION ---------------- */

void promoteWaitingForBus(int busType, int busIndex)
{
    Bus *bus = &fleet[busType][busIndex];
    int avail = getAvailableSeatsInBus(busType, busIndex);
    if (avail <= 0 || front == NULL)
        return;

    printf("\n--- Automatic Waiting List Promotion (Bus #%d Available Seats: %d) ---\n",
           busIndex + 1, avail);

    /* PASS 1: Group Preference Pass */
    int promotedAny = 1;
    while (avail > 0 && promotedAny)
    {
        promotedAny = 0;
        QNode *curr = front;

        while (curr != NULL)
        {
            QNode *nextNode = curr->next;
            Booking *wb = curr->booking;

            if (wb->busType == busType && wb->busIndex == busIndex && wb->count >= 2)
            {
                if (avail >= wb->count && autoAllocateWaitingBooking(bus, wb))
                {
                    removeWaitingByNode(curr);
                    addConfirmed(wb);
                    avail = getAvailableSeatsInBus(busType, busIndex);
                    promotedAny = 1;

                    printf("  [Group Promoted] Booking ID %d (Group of %d) promoted to Confirmed!\n",
                           wb->id, wb->count);
                    for (int i = 0; i < wb->count; i++)
                    {
                        printf("    -> %s (%c) assigned Seat %d\n",
                               wb->passengers[i].name, wb->passengers[i].gender, wb->passengers[i].seat);
                    }
                    break;
                }
            }
            curr = nextNode;
        }
    }

    /* PASS 2: Fill remaining available seats */
    promotedAny = 1;
    while (avail > 0 && promotedAny)
    {
        promotedAny = 0;
        QNode *curr = front;

        while (curr != NULL)
        {
            QNode *nextNode = curr->next;
            Booking *wb = curr->booking;

            if (wb->busType == busType && wb->busIndex == busIndex)
            {
                if (wb->count > avail)
                {
                    printf("  [Skipped Group] Booking ID %d needs %d seats (only %d left). Looking for others...\n",
                           wb->id, wb->count, avail);
                }
                else if (autoAllocateWaitingBooking(bus, wb))
                {
                    removeWaitingByNode(curr);
                    addConfirmed(wb);
                    avail = getAvailableSeatsInBus(busType, busIndex);
                    promotedAny = 1;

                    printf("  [Promoted] Booking ID %d (%s, Count: %d) promoted to Confirmed!\n",
                           wb->id, wb->passengers[0].name, wb->count);
                    for (int i = 0; i < wb->count; i++)
                    {
                        printf("    -> %s (%c) assigned Seat %d %s\n",
                               wb->passengers[i].name, wb->passengers[i].gender, wb->passengers[i].seat,
                               wb->passengers[i].exceptionMark ? "[MARKED: Last-Seat Exception]" : "");
                    }
                    break;
                }
            }
            curr = nextNode;
        }
    }
}

/* ---------------- CANCELLATION & DIRECT SEAT DISAMBIGUATION ---------------- */

/*
 * When cancelling a member from a group booking:
 * - Asks for the passenger's name.
 * - If multiple passengers share the same name, directly asks for the Seat Number
 *   (or list number if the booking is still on the Waiting List) without asking
 *   Age, Gender, and Phone first.
 */
int selectGroupMemberToCancel(Booking *b)
{
    char targetName[50];
    int candidates[MAX_SEATS_PER_BUS + 1];
    int candCount = 0;

    while (1)
    {
        printf("\nCurrent members in Booking ID %d:\n", b->id);
        for (int i = 0; i < b->count; i++)
        {
            printf("  %d. Name: %-15s | Gender: %c | Age: %2d | Phone: %-10s | Seat: %d\n",
                   i + 1, b->passengers[i].name, b->passengers[i].gender,
                   b->passengers[i].age, b->passengers[i].phone, b->passengers[i].seat);
        }

        readName("\nEnter the name of the passenger to cancel: ", targetName, sizeof(targetName));

        candCount = 0;
        for (int i = 0; i < b->count; i++)
        {
            if (strIgnoreCaseCmp(b->passengers[i].name, targetName) == 0)
                candidates[candCount++] = i;
        }

        if (candCount == 0)
        {
            printf("  [!] No passenger named '%s' found in this group. Try again.\n", targetName);
            continue;
        }

        if (candCount == 1)
            return candidates[0];

        /* Multiple passengers share the same name -> go for the Seat Number directly */
        printf("\n[Disambiguation Required] %d passengers in this group have the same name '%s'!\n",
               candCount, targetName);

        if (b->status == 1)
        {
            while (1)
            {
                int targetSeat = readInt("Enter Seat Number to cancel directly: ");
                for (int i = 0; i < candCount; i++)
                {
                    int idx = candidates[i];
                    if (b->passengers[idx].seat == targetSeat)
                        return idx;
                }
                printf("  invalid choice! try again (Enter a valid Seat Number for '%s')\n", targetName);
            }
        }
        else
        {
            while (1)
            {
                int memberNo = readInt("Enter passenger list number (1 to N) to cancel directly: ");
                if (memberNo >= 1 && memberNo <= b->count &&
                    strIgnoreCaseCmp(b->passengers[memberNo - 1].name, targetName) == 0)
                {
                    return memberNo - 1;
                }
                printf("  invalid choice! try again\n");
            }
        }
    }
}

void cancelTicket(void)
{
    clearScreen();
    printAppBanner("TICKET CANCELLATION");

    int id = readInt("Enter Booking ID to cancel (or 0 to go back): ");
    if (id == 0)
    {
        clearScreen();
        printAppBanner("Cancellation Aborted");
        return;
    }

    Booking *b = searchBooking(id);
    if (b == NULL)
    {
        clearScreen();
        printAppBanner("CANCELLATION OUTPUT");
        printf("[!] Booking ID %d not found.\n", id);
        return;
    }

    int busType = b->busType;
    int busIndex = b->busIndex;
    Bus *bus = &fleet[busType][busIndex];

    int cancelCount = 1;
    if (b->count >= 2)
    {
        printf("\nBooking ID %d is a Group Reservation with %d passengers.\n", b->id, b->count);
        while (1)
        {
            cancelCount = readInt("Enter count of people to cancel: ");
            if (cancelCount >= 1 && cancelCount <= b->count)
                break;
            printf("invalid choice! try again\n");
        }
    }

    if (b->count == 1)
    {
        clearScreen();
        printAppBanner("CANCELLATION & PROMOTION OUTPUT");

        if (b->status == 0)
        {
            printf("Waiting-list booking ID %d (%s) cancelled.\n", b->id, b->passengers[0].name);
            removeWaitingById(b->id);
            deleteHash(b->id);
            free(b->passengers);
            free(b);
            return;
        }

        int seat = b->passengers[0].seat;
        bus->seats[seat].occupied = 0;
        bus->seats[seat].bookingId = 0;
        bus->seats[seat].passengerName[0] = '\0';
        bus->seats[seat].gender = '\0';
        bus->seats[seat].exceptionMark = 0;

        printf("Confirmed booking ID %d for '%s' (Seat %d) cancelled.\n",
               b->id, b->passengers[0].name, seat);

        removeConfirmed(b->id);
        deleteHash(b->id);
        free(b->passengers);
        free(b);

        promoteWaitingForBus(busType, busIndex);
        printBusSeatLayout(busType, busIndex);
        return;
    }

    char cancelledLog[512] = "";
    for (int c = 0; c < cancelCount; c++)
    {
        printf("\n--- Removing Passenger %d of %d ---\n", c + 1, cancelCount);
        int idx = selectGroupMemberToCancel(b);
        Passenger removed = b->passengers[idx];

        char line[100];
        if (b->status == 1 && removed.seat >= 1 && removed.seat <= bus->totalSeats)
        {
            int s = removed.seat;
            bus->seats[s].occupied = 0;
            bus->seats[s].bookingId = 0;
            bus->seats[s].passengerName[0] = '\0';
            bus->seats[s].gender = '\0';
            bus->seats[s].exceptionMark = 0;
            printf("Removed '%s' (Seat %d freed).\n", removed.name, s);
            snprintf(line, sizeof(line), "  - Removed '%s' (Seat %d freed)\n", removed.name, s);
        }
        else
        {
            printf("Removed '%s' from Waiting List group.\n", removed.name);
            snprintf(line, sizeof(line), "  - Removed '%s' from Waiting List group\n", removed.name);
        }
        strncat(cancelledLog, line, sizeof(cancelledLog) - strlen(cancelledLog) - 1);

        for (int j = idx; j < b->count - 1; j++)
            b->passengers[j] = b->passengers[j + 1];

        b->count--;
        b->totalFare = b->count * BUS_TYPE_FARES[b->busType];
        b->isGroup = (b->count >= 2) ? 1 : 0;
    }

    int wasConfirmed = (b->status == 1);

    clearScreen();
    printAppBanner("GROUP CANCELLATION & PROMOTION OUTPUT");
    printf("%s", cancelledLog);

    if (b->count == 0)
    {
        if (wasConfirmed)
            removeConfirmed(b->id);
        else
            removeWaitingById(b->id);

        deleteHash(b->id);
        free(b->passengers);
        free(b);
        printf("All members of Group Booking ID %d have been cancelled.\n", id);
    }
    else
    {
        printf("%d passenger(s) cancelled. %d passenger(s) remain in Booking ID %d (New Fare: Rs. %.2f).\n",
               cancelCount, b->count, b->id, b->totalFare);
    }

    if (wasConfirmed)
    {
        promoteWaitingForBus(busType, busIndex);
        printBusSeatLayout(busType, busIndex);
    }
}

/* ---------------- SEARCH & DISPLAY OUTPUT FUNCTIONS ---------------- */

void searchTicket(void)
{
    clearScreen();
    printAppBanner("SEARCH BOOKING");
    int id = readInt("Enter Booking ID to search: ");
    Booking *b = searchBooking(id);

    clearScreen();
    printAppBanner("BOOKING SEARCH OUTPUT");

    if (b == NULL)
    {
        printf("Booking ID %d not found.\n", id);
        return;
    }

    printf("Booking ID : %d\n", b->id);
    printf("Bus Type   : %s (Bus #%d)\n", BUS_TYPE_NAMES[b->busType], b->busIndex + 1);
    printf("Status     : %s\n", b->status ? "Confirmed" : "Waiting List");
    printf("Category   : %s (%d passenger(s))\n", b->isGroup ? "Group" : "Single", b->count);
    printf("Total Fare : Rs. %.2f\n", b->totalFare);

    for (int i = 0; i < b->count; i++)
    {
        printf("  %d. Name: %-15s | Gender: %c | Age: %2d | Phone: %-10s | Seat: %d %s\n",
               i + 1, b->passengers[i].name, b->passengers[i].gender,
               b->passengers[i].age, b->passengers[i].phone, b->passengers[i].seat,
               b->passengers[i].exceptionMark ? "[MARKED: Opposite-Gender / Last-Seat]" : "");
    }
}

void displaySeats(void)
{
    clearScreen();
    printAppBanner("LIVE FLEET & SEAT AVAILABILITY OUTPUT");

    for (int t = 0; t < NUM_BUS_TYPES; t++)
    {
        int availBuses = getAvailableBusCount(t);
        printf("\n>>> %s (Available Buses: %d / %d | Fare: Rs. %.2f/seat)\n",
               BUS_TYPE_NAMES[t], availBuses, totalBusesOfType[t], BUS_TYPE_FARES[t]);

        for (int b = 0; b < totalBusesOfType[t]; b++)
        {
            printBusSeatLayout(t, b);
        }
    }
}

void display(void)
{
    clearScreen();
    printAppBanner("FINAL CONFIRMED BOOKINGS LIST (display())");

    Booking *temp = confirmedHead;
    if (temp == NULL)
    {
        printf("No confirmed bookings currently.\n");
        return;
    }

    while (temp != NULL)
    {
        printf("\nBooking ID: %d | Bus: %s (Bus #%d) | %s (Count: %d) | Total Fare: Rs. %.2f\n",
               temp->id, BUS_TYPE_NAMES[temp->busType], temp->busIndex + 1,
               temp->isGroup ? "Group" : "Single", temp->count, temp->totalFare);

        for (int i = 0; i < temp->count; i++)
        {
            printf("   -> Seat %02d | Name: %-15s | Gender: %c | Age: %2d | Phone: %-10s %s\n",
                   temp->passengers[i].seat,
                   temp->passengers[i].name,
                   temp->passengers[i].gender,
                   temp->passengers[i].age,
                   temp->passengers[i].phone,
                   temp->passengers[i].exceptionMark
                       ? "<-- [MARKED: Opposite-Gender Adjacent / Last-Seat Exception]"
                       : "");
        }
        temp = temp->next;
    }
}

void displayWaiting(void)
{
    clearScreen();
    printAppBanner("CURRENT WAITING LIST OUTPUT");

    QNode *temp = front;
    int position = 1;

    if (temp == NULL)
    {
        printf("Waiting list is currently empty.\n");
        return;
    }

    while (temp != NULL)
    {
        Booking *b = temp->booking;
        printf("\nPosition #%d | Booking ID: %d | Bus: %s (Bus #%d) | Count: %d (%s)\n",
               position++, b->id, BUS_TYPE_NAMES[b->busType], b->busIndex + 1,
               b->count, b->isGroup ? "Group" : "Single");

        for (int i = 0; i < b->count; i++)
        {
            printf("   -> Name: %-15s | Gender: %c | Age: %2d | Phone: %s\n",
                   b->passengers[i].name, b->passengers[i].gender,
                   b->passengers[i].age, b->passengers[i].phone);
        }
        temp = temp->next;
    }
}

/* ---------------- OFFICIAL MENU FOR BUS & SEAT MANAGEMENT ---------------- */

void officialMenu(void)
{
    clearScreen();
    printAppBanner("OFFICIAL / ADMIN BUS FLEET MENU");

    while (1)
    {
        printf("\n===== OFFICIAL / ADMIN BUS FLEET MENU =====\n");
        for (int t = 0; t < NUM_BUS_TYPES; t++)
        {
            printf("%d. %s\n", t + 1, BUS_TYPE_NAMES[t]);
        }
        printf("0. Back to Main Menu\n");

        int choice = readInt("Enter official choice: ");
        if (choice == 0)
        {
            clearScreen();
            printAppBanner("Returned from Official Menu");
            return;
        }

        if (choice < 1 || choice > NUM_BUS_TYPES)
        {
            clearScreen();
            printAppBanner("OFFICIAL / ADMIN BUS FLEET MENU");
            printf("invalid choice! try again\n");
            continue;
        }

        int t = choice - 1;

        clearScreen();
        printAppBanner("BUS TYPE DETAILS & SEAT AVAILABILITY");
        printf("\nBus Type             : %s\n", BUS_TYPE_NAMES[t]);
        printf("Total Buses in Type  : %d\n", totalBusesOfType[t]);
        printf("Available Buses      : %d\n", getAvailableBusCount(t));
        printf("Seats Available in Each Bus:\n");
        for (int b = 0; b < totalBusesOfType[t]; b++)
        {
            printf("  -> Bus #%d : %d / %d seats available\n",
                   b + 1, getAvailableSeatsInBus(t, b), fleet[t][b].totalSeats);
        }

        int wantUpdate = 0;
        while (1)
        {
            wantUpdate = readInt("\nDo you want to update? (1 = Yes, 2 = No): ");
            if (wantUpdate == 1 || wantUpdate == 2)
                break;
            printf("invalid choice! try again\n");
        }

        if (wantUpdate == 2)
        {
            clearScreen();
            printAppBanner("OFFICIAL / ADMIN BUS FLEET MENU");
            continue;
        }

        printf("\n--- Modification Options for %s ---\n", BUS_TYPE_NAMES[t]);
        printf("1. Make modification in the number of available buses\n");
        printf("2. Make modification in the number of available seats per bus\n");

        int modChoice = 0;
        while (1)
        {
            modChoice = readInt("Select modification option (1 or 2): ");
            if (modChoice == 1 || modChoice == 2)
                break;
            printf("invalid choice! try again\n");
        }

        if (modChoice == 1)
        {
            char prompt[100];
            snprintf(prompt, sizeof(prompt),
                     "Enter new number of available buses for %s (1-%d): ",
                     BUS_TYPE_NAMES[t], MAX_BUSES_PER_TYPE);
            int newBusCount = readInt(prompt);

            if (newBusCount < 1 || newBusCount > MAX_BUSES_PER_TYPE)
            {
                clearScreen();
                printAppBanner("OFFICIAL UPDATE FAILED");
                printf("invalid choice! try again (Bus count must be between 1 and %d).\n", MAX_BUSES_PER_TYPE);
                continue;
            }

            if (newBusCount < totalBusesOfType[t])
            {
                int canShrink = 1;
                for (int b = newBusCount; b < totalBusesOfType[t]; b++)
                {
                    if (getAvailableSeatsInBus(t, b) < fleet[t][b].totalSeats)
                    {
                        canShrink = 0;
                        clearScreen();
                        printAppBanner("OFFICIAL UPDATE BLOCKED");
                        printf("[!] Cannot reduce bus count to %d because Bus #%d has active bookings.\n",
                               newBusCount, b + 1);
                        break;
                    }
                }
                if (!canShrink)
                    continue;
            }
            else if (newBusCount > totalBusesOfType[t])
            {
                for (int b = totalBusesOfType[t]; b < newBusCount; b++)
                {
                    fleet[t][b].totalSeats = seatsPerBusType[t];
                    for (int s = 1; s <= MAX_SEATS_PER_BUS; s++)
                    {
                        fleet[t][b].seats[s].occupied = 0;
                        fleet[t][b].seats[s].bookingId = 0;
                        fleet[t][b].seats[s].passengerName[0] = '\0';
                        fleet[t][b].seats[s].gender = '\0';
                        fleet[t][b].seats[s].exceptionMark = 0;
                    }
                }
            }

            totalBusesOfType[t] = newBusCount;

            clearScreen();
            printAppBanner("OFFICIAL BUS COUNT UPDATED");
            printf("Successfully updated '%s' total buses to %d.\n",
                   BUS_TYPE_NAMES[t], totalBusesOfType[t]);

            for (int b = 0; b < totalBusesOfType[t]; b++)
            {
                promoteWaitingForBus(t, b);
            }
        }
        else if (modChoice == 2)
        {
            char prompt[100];
            snprintf(prompt, sizeof(prompt),
                     "Enter new number of available seats per bus for %s (1-%d): ",
                     BUS_TYPE_NAMES[t], MAX_SEATS_PER_BUS);
            int newSeatCount = readInt(prompt);

            if (newSeatCount < 1 || newSeatCount > MAX_SEATS_PER_BUS)
            {
                clearScreen();
                printAppBanner("OFFICIAL UPDATE FAILED");
                printf("invalid choice! try again (Seats per bus must be between 1 and %d).\n", MAX_SEATS_PER_BUS);
                continue;
            }

            int canResize = 1;
            int conflictBus = -1;
            int conflictSeat = -1;

            for (int b = 0; b < totalBusesOfType[t]; b++)
            {
                for (int s = newSeatCount + 1; s <= fleet[t][b].totalSeats; s++)
                {
                    if (fleet[t][b].seats[s].occupied)
                    {
                        canResize = 0;
                        conflictBus = b + 1;
                        conflictSeat = s;
                        break;
                    }
                }
                if (!canResize)
                    break;
            }

            if (!canResize)
            {
                clearScreen();
                printAppBanner("OFFICIAL UPDATE BLOCKED");
                printf("[!] Cannot reduce seats per bus to %d: Bus #%d has an active booking in Seat %d.\n",
                       newSeatCount, conflictBus, conflictSeat);
                continue;
            }

            seatsPerBusType[t] = newSeatCount;
            for (int b = 0; b < MAX_BUSES_PER_TYPE; b++)
            {
                fleet[t][b].totalSeats = newSeatCount;
            }

            clearScreen();
            printAppBanner("OFFICIAL SEATS PER BUS UPDATED");
            printf("Successfully updated '%s' seats per bus to %d.\n",
                   BUS_TYPE_NAMES[t], seatsPerBusType[t]);
            for (int b = 0; b < totalBusesOfType[t]; b++)
            {
                printf("  -> Bus #%d now has %d / %d seats available\n",
                       b + 1, getAvailableSeatsInBus(t, b), fleet[t][b].totalSeats);
                promoteWaitingForBus(t, b);
            }
        }
    }
}

/* ---------------- MEMORY CLEANUP ---------------- */

void cleanup(void)
{
    while (confirmedHead != NULL)
    {
        Booking *temp = confirmedHead;
        confirmedHead = confirmedHead->next;
        free(temp->passengers);
        free(temp);
    }

    while (front != NULL)
    {
        QNode *temp = front;
        front = front->next;
        free(temp->booking->passengers);
        free(temp->booking);
        free(temp);
    }

    for (int i = 0; i < TABLE_SIZE; i++)
    {
        while (hashTable[i] != NULL)
        {
            HashEntry *temp = hashTable[i];
            hashTable[i] = hashTable[i]->next;
            free(temp);
        }
    }
}

/* ---------------- MAIN REAL-TIME LOOP ---------------- */

int main(void)
{
    initFleet();
    clearScreen();
    printAppBanner("WELCOME TO REAL-TIME BUS RESERVATION");

    int choice;
    while (1)
    {
        printf("\n+----------------------------------------------------------------------+\n");
        printf("|                             MAIN MENU                                |\n");
        printf("+----------------------------------------------------------------------+\n");
        printf("|  1. Book Ticket (Choose Bus, Seat Number & Group/Solo)               |\n");
        printf("|  2. Cancel Ticket (Group/Individual & Auto Waiting Promotion)        |\n");
        printf("|  3. Search Booking by ID                                             |\n");
        printf("|  4. Display Live Seat & Bus Availability                             |\n");
        printf("|  5. Display Final Confirmed Bookings List (display())                |\n");
        printf("|  6. Display Waiting List                                             |\n");
        printf("|  7. Official Menu (View / Modify Bus & Seat Counts)                  |\n");
        printf("|  0. Exit Application                                                 |\n");
        printf("+----------------------------------------------------------------------+\n");

        choice = readInt("Select an option: ");

        switch (choice)
        {
            case 1:
                bookTicket();
                break;
            case 2:
                cancelTicket();
                break;
            case 3:
                searchTicket();
                break;
            case 4:
                displaySeats();
                break;
            case 5:
                display();
                break;
            case 6:
                displayWaiting();
                break;
            case 7:
                officialMenu();
                break;
            case 0:
                cleanup();
                clearScreen();
                printAppBanner("SESSION CLOSED");
                printf("Thank you for using the Public Transport Reservation System!\n");
                return 0;
            default:
                clearScreen();
                printAppBanner("INVALID MENU OPTION");
                printf("invalid choice! try again\n");
        }
    }
}