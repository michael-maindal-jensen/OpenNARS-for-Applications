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

#include "Decision.h"

double DECISION_THRESHOLD = DECISION_THRESHOLD_INITIAL;
double ANTICIPATION_THRESHOLD = ANTICIPATION_THRESHOLD_INITIAL;
double ANTICIPATION_CONFIDENCE = ANTICIPATION_CONFIDENCE_INITIAL;
double MOTOR_BABBLING_CHANCE = MOTOR_BABBLING_CHANCE_INITIAL;
//Inject action event after execution or babbling
void Decision_Execute(Decision *decision)
{
    assert(decision->operationID > 0, "Operation 0 is reserved for no action");
    decision->op = operations[decision->operationID-1];
    (*decision->op.action)(decision->arguments);
    //and add operator feedback
    if(decision->arguments.atoms[0] > 0) //operation with args
    {
        Term operation = {0};
        operation.atoms[0] = Narsese_AtomicTermIndex(":"); //<args --> ^op>
        Term_OverrideSubterm(&operation, 1, &decision->arguments);
        Term_OverrideSubterm(&operation, 2, &decision->op.term);
        NAR_AddInputBelief(operation);
    }
    else //atomic operation / operator
    {
        NAR_AddInputBelief(decision->op.term);
    }
}

//"reflexes" to try different operations, especially important in the beginning
static Decision Decision_MotorBabbling()
{
    Decision decision = (Decision) {0};
    int n_ops = 0;
    for(int i=0; i<OPERATIONS_MAX && operations[i].action != 0; i++)
    {
        n_ops = i+1;
    }
    if(n_ops > 0)
    {
        decision.operationID = 1+(rand() % (n_ops));
        IN_DEBUG (
            printf(" NAR BABBLE %d\n", decision.operationID);
        )
        decision.execute = true;
    }
    return decision;
}

static Decision Decision_ConsiderImplication(long currentTime, Event *goal, int considered_opi, Implication *imp, Implication *DebugBestImp)
{
    Decision decision = (Decision) {0};
    IN_DEBUG
    (
        printf("CONSIDERED IMPLICATION: impTruth=(%f, %f)", imp->truth.frequency, imp->truth.confidence);
        Narsese_PrintTerm(&imp->term);
        puts("");
    )
    //now look at how much the precondition is fulfilled
    Concept *prec = imp->sourceConcept;
    Event *precondition = &prec->belief_spike; //a. :|:
    if(precondition != NULL)
    {
        Event ContextualOperation = Inference_GoalDeduction(goal, imp); //(&/,a,op())! :\:
        double operationGoalTruthExpectation = Truth_Expectation(Inference_OperationDeduction(&ContextualOperation, precondition, currentTime).truth); //op()! :|:
        IN_DEBUG
        (
            printf("CONSIDERED PRECON: desire=%f ", operationGoalTruthExpectation);
            Narsese_PrintTerm(&prec->term);
            fputs("\nCONSIDERED PRECON truth ", stdout);
            Truth_Print(&precondition->truth);
            fputs("CONSIDERED goal truth ", stdout);
            Truth_Print(&goal->truth);
            fputs("CONSIDERED imp truth ", stdout);
            Truth_Print(&imp->truth);
            printf("CONSIDERED time %ld\n", precondition->occurrenceTime);
            Narsese_PrintTerm(&precondition->term); puts("");
        )
        *DebugBestImp = *imp;
        //<(precon &/ <args --> ^op>) =/> postcon>. -> [$ , postcon precon : _ _ _ _ args ^op
        Term operation = Term_ExtractSubterm(&imp->term, 4); //^op or [: args ^op]
        if(!Narsese_isOperator(operation.atoms[0])) //it is an operation with args, not just an atomic operator, so remember the args
        {
            assert(Narsese_isOperator(operation.atoms[2]), "If it's not atomic, it needs to be an operation with args here");
            decision.arguments = Term_ExtractSubterm(&imp->term, 9); 
        }
        decision.operationID = considered_opi;
        decision.desire = operationGoalTruthExpectation;
    }
    return decision;
}

int stampID = -1;
Decision Decision_BestCandidate(Event *goal, long currentTime)
{
    Decision decision = (Decision) {0};
    Implication bestImp = {0};
    Concept *cbest_predicate = NULL;
    for(int concept_i=0; concept_i<concepts.itemsAmount; concept_i++)
    {
        Concept *postc_general = concepts.items[concept_i].address;
        Substitution subs = Variable_Unify(&postc_general->term, &goal->term);
        if(subs.success)
        {
            for(int opi=1; opi<OPERATIONS_MAX && operations[opi-1].action != 0; opi++)
            {
                for(int j=0; j<postc_general->precondition_beliefs[opi].itemsAmount; j++)
                {
                    if(!Memory_ImplicationValid(&postc_general->precondition_beliefs[opi].array[j]))
                    {
                        Table_Remove(&postc_general->precondition_beliefs[opi], j--);
                        continue;
                    }
                    Implication imp = postc_general->precondition_beliefs[opi].array[j];
                    bool success;
                    imp.term = Variable_ApplySubstitute(imp.term, subs, &success);
                    if(success)
                    {
                        assert(Narsese_copulaEquals(imp.term.atoms[0], '$'), "This should be an implication!");
                        Term left_side_with_op = Term_ExtractSubterm(&imp.term, 1);
                        Term left_side = Narsese_GetPreconditionWithoutOp(&left_side_with_op); //might be something like <#1 --> a>
                        for(int cmatch_k=0; cmatch_k<concepts.itemsAmount; cmatch_k++)
                        {
                            Concept *cmatch = concepts.items[cmatch_k].address;
                            if(!Variable_hasVariable(&cmatch->term, true, true, true))
                            {
                                Substitution subs2 = Variable_Unify(&left_side, &cmatch->term);
                                if(subs2.success)
                                {
                                    Implication specific_imp = imp; //can only be completely specific
                                    bool success;
                                    specific_imp.term = Variable_ApplySubstitute(specific_imp.term, subs2, &success);
                                    if(success && !Variable_hasVariable(&specific_imp.term, true, true, true))
                                    {
                                        specific_imp.sourceConcept = cmatch;
                                        specific_imp.sourceConceptId = cmatch->id;
                                        Decision considered = Decision_ConsiderImplication(currentTime, goal, opi, &specific_imp, &bestImp);
                                        if(considered.desire > decision.desire)
                                        {
                                            decision = considered;
                                            cbest_predicate = cmatch;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    if(decision.desire < DECISION_THRESHOLD)
    {
        return decision;
    }
    //increase usefulness
    assert(cbest_predicate != NULL, "Above decision threshold but postcondition concept is NULL!");
    cbest_predicate->usage = Usage_use(cbest_predicate->usage, currentTime);
    Term subject_with_op = Term_ExtractSubterm(&bestImp.term, 1);
    Term subject = Narsese_GetPreconditionWithoutOp(&subject_with_op);
    Concept *cbest_subject = Memory_Conceptualize(&subject, currentTime);
    if(cbest_subject != NULL)
    {
        cbest_subject->usage = Usage_use(cbest_subject->usage, currentTime);
    }
    //set execute and return execution
    printf("decision expectation %f impTruth=(%f, %f): future=%ld ", decision.desire, bestImp.truth.frequency, bestImp.truth.confidence, bestImp.occurrenceTimeOffset);
    Narsese_PrintTerm(&bestImp.term); puts("");
    decision.execute = true;
    return decision;
}

void Decision_AssumptionOfFailure(int operationID, long currentTime)
{
    assert(operationID >= 0 && operationID <= OPERATIONS_MAX, "Wrong operation id, did you inject an event manually?");
    for(int j=0; j<concepts.itemsAmount; j++)
    {
        Concept *postc = concepts.items[j].address;
        for(int  h=0; h<postc->precondition_beliefs[operationID].itemsAmount; h++)
        {
            if(!Memory_ImplicationValid(&postc->precondition_beliefs[operationID].array[h]))
            {
                Table_Remove(&postc->precondition_beliefs[operationID], h);
                h--;
                continue;
            }
            Implication imp = postc->precondition_beliefs[operationID].array[h]; //(&/,a,op) =/> b.
            Concept *current_prec = imp.sourceConcept;
            Event *precondition = &current_prec->belief_spike; //a. :|:
            if(precondition != NULL && precondition->type != EVENT_TYPE_DELETED)
            {
                assert(precondition->occurrenceTime != OCCURRENCE_ETERNAL, "Precondition should not be eternal!");
                Event updated_precondition = Inference_EventUpdate(precondition, currentTime);
                Event op = { .type = EVENT_TYPE_BELIEF,
                             .truth = (Truth) { .frequency = 1.0, .confidence = 0.9 },
                             .occurrenceTime = currentTime };
                Event seqop = Inference_BeliefIntersection(&updated_precondition, &op); //(&/,a,op). :|:
                Event result = Inference_BeliefDeduction(&seqop, &imp); //b. :/:
                if(Truth_Expectation(result.truth) > ANTICIPATION_THRESHOLD)
                {
                    Implication negative_confirmation = imp;
                    Truth TNew = { .frequency = 0.0, .confidence = ANTICIPATION_CONFIDENCE };
                    Truth TPast = Truth_Projection(precondition->truth, 0, imp.occurrenceTimeOffset);
                    negative_confirmation.truth = Truth_Eternalize(Truth_Induction(TPast, TNew));
                    negative_confirmation.stamp = (Stamp) { .evidentalBase = { -stampID } };
                    assert(negative_confirmation.truth.confidence >= 0.0 && negative_confirmation.truth.confidence <= 1.0, "(666) confidence out of bounds");
                    Implication *added = Table_AddAndRevise(&postc->precondition_beliefs[operationID], &negative_confirmation);
                    if(added != NULL)
                    {
                        added->sourceConcept = negative_confirmation.sourceConcept;
                        added->sourceConceptId = negative_confirmation.sourceConceptId;
                    }                                
                    stampID--;
                }
            }
        }
    }
}

Decision Decision_Suggest(Event *goal, long currentTime)
{
    Decision babble_decision = {0};
    //try motor babbling with a certain chance
    if(rand() % 1000000 < (int)(MOTOR_BABBLING_CHANCE*1000000.0))
    {
        babble_decision = Decision_MotorBabbling();
    }
    //try matching op if didn't motor babble
    Decision decision_suggested = Decision_BestCandidate(goal, currentTime);
    if(!babble_decision.execute || decision_suggested.desire > MOTOR_BABBLING_SUPPRESSION_THRESHOLD)
    {
       return decision_suggested;
    }
    return babble_decision;
}
