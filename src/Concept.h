/* 
 * The MIT License
 *
 * Copyright 2020 The OpenNARS authors.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef CONCEPT_H
#define CONCEPT_H

///////////////////
//  Term Concept  //
///////////////////
//A concept named by a Term

//References//
//-----------//
#include "FIFO.h"
#include "Table.h"
#include "Usage.h"

//Data structure//
//--------------//
typedef struct {
    long id;
    Usage usage;
    Term term;
    TERM_HASH_TYPE term_hash;
    Event belief; //the highest confident eternal belief
    Event belief_spike;
    Event predicted_belief;
    Event incoming_goal_spike;
    Event goal_spike;
    Table precondition_beliefs[OPERATIONS_MAX];
    double priority;
    bool hasUserKnowledge;
} Concept;

//Methods//
//-------//
//Assign a new name to a concept
void Concept_SetTerm(Concept *concept, Term term);
//print a concept
void Concept_Print(Concept *concept);

#endif
