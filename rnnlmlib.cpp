///////////////////////////////////////////////////////////////////////
//
// Recurrent neural network based statistical language modeling toolkit
// Version 0.4a
// (c) 2010-2012 Tomas Mikolov (tmikolov@gmail.com)
// (c) 2013 Cantab Research Ltd (info@cantabResearch.com)
//
///////////////////////////////////////////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <cfloat>
#include "fastexp.h"
#include "types.h"
#include "neuron.h"
#include "synapse.h"
#include "vocabulary.h"
#include "word_class.h"
#include "layer.h"
#include "matrix.h"
#include "backpropagation.h"
#include "direct.h"
#include "rnnlmlib.h"

///// include blas
#ifdef USE_BLAS
extern "C" {
#include <cblas.h>
}
#endif

void CRnnLM::setHiddenLayerSize(int newsize) {
	layer1.initialize(newsize);
	layer1.clear();
}

void CRnnLM::setCompressionLayerSize(int newsize) {
	layerc.initialize(newsize);
	layerc.clear();
}

void CRnnLM::setTrainFile(char *str)
{
	strcpy(train_file, str);
}

void CRnnLM::setValidFile(char *str)
{
	strcpy(valid_file, str);
}

void CRnnLM::setTestFile(char *str)
{
	strcpy(test_file, str);
}

void CRnnLM::setRnnLMFile(char *str)
{
	strcpy(rnnlm_file, str);
}

// returns -1 if EOF or not in vocab
int CRnnLM::readWordIndex(FILE *fin)
{
	char word[MAX_STRING];

	vocab.readWord(word, fin);
	if (feof(fin))
		return -1;
	else
		return vocab.search(word);
}

void CRnnLM::saveWeights()      //saves current weights and unit activations
{
	layer0.backup(0);
	layer1.backup(0);
	layer2.backup(0);
    
	matrix01.backup();
	matrix12.backup();

	if (layerc._size>0) {
		layerc.backup(0);
		matrixc2.backup();
	}
}

void CRnnLM::restoreWeights()      //restores current weights and unit activations from backup copy
{
	layer0.clear();
	layer1.clear();
	layer2.clear();

	matrix01.restore();
	matrix12.restore();

	if (layerc._size>0) {
		layerc.clear();
		matrixc2.restore();
	}
}

void CRnnLM::initialize()
{
	// layer 0 includes neurons for vocabulary and 
	// neurons for prior layer 1
	layer0.initialize(vocab._size + layer1._size);
	layer0.clear();

	matrix01.initialize(layer0._size, layer1._size);
	matrix01.randomize();

	// Output layer 2 includes neurons for vocabulary and
	// neurons for word classes
	layer2.initialize(vocab._size + wordClass._size);
	layer2.clear();
	
	if (layerc._size == 0) {
		matrix12.initialize(layer1._size, layer2._size);
		matrix12.randomize();
	} else {
		// matrix12 actually maps layer 1 to layer c
		matrix12.initialize(layer1._size, layerc._size);
		matrix12.randomize();

		// matrix c2 maps layer c to output layer 2
		matrixc2.initialize(layerc._size, layer2._size);
		matrixc2.randomize();
	}
    
	direct.initialize();

	bp.initialize(layer0._size, layer1._size);

	saveWeights();

	if (old_classes) {
		vocab.setClassIndexOld(wordClass._size);
	} else {
		vocab.setClassIndexNew(wordClass._size);
	}
    
	//allocate auxiliary class variables (for faster search when normalizing probability at output layer)
    wordClass.initialize(vocab);
}

void CRnnLM::saveNet()       //will save the whole network structure                                                        
{
	FILE *fo;
	char str[1000];
    
	sprintf(str, "%s.temp", rnnlm_file);

	fo=fopen(str, "wb");
	if (fo==NULL) {
		printf("Cannot create file %s\n", rnnlm_file);
		exit(1);
	}
	fprintf(fo, "version: %d\n", version);
	fprintf(fo, "file format: %d\n\n", filetype);

	fprintf(fo, "training data file: %s\n", train_file);
	fprintf(fo, "validation data file: %s\n\n", valid_file);

	fprintf(fo, "last probability of validation data: %f\n", llogp);
	fprintf(fo, "number of finished iterations: %d\n", iter);

	fprintf(fo, "current position in training data: %d\n", train_cur_pos);
	fprintf(fo, "current probability of training data: %f\n", logp);
	fprintf(fo, "save after processing # words: %d\n", anti_k);
	fprintf(fo, "# of training words: %d\n", train_words);

	fprintf(fo, "input layer size: %d\n", layer0._size);
	fprintf(fo, "hidden layer size: %d\n", layer1._size);
	fprintf(fo, "compression layer size: %d\n", layerc._size);
	fprintf(fo, "output layer size: %d\n", layer2._size);

	fprintf(fo, "direct connections: %lld\n", direct._size);
	fprintf(fo, "direct order: %d\n", direct._order);
    
	fprintf(fo, "bptt: %d\n", bp._bptt);
	fprintf(fo, "bptt block: %d\n", bp._block);
    
	fprintf(fo, "vocabulary size: %d\n", vocab._size);
	fprintf(fo, "class size: %d\n", wordClass._size);
    
	fprintf(fo, "old classes: %d\n", old_classes);
	fprintf(fo, "independent sentences mode: %d\n", independent);
    
	fprintf(fo, "starting learning rate: %f\n", starting_alpha);
	fprintf(fo, "current learning rate: %f\n", alpha);
	fprintf(fo, "learning rate decrease: %d\n", alpha_divide);
	fprintf(fo, "\n");

	fprintf(fo, "\nVocabulary:\n");
	vocab.print(fo);

    
	if (filetype==TEXT) {
		fprintf(fo, "\nHidden layer activation:\n");
		layer1.print(fo);
	}
	if (filetype==BINARY) {
		layer1.write(fo);
	}
	//////////
	if (filetype==TEXT) {
		fprintf(fo, "\nWeights 0->1:\n");
		matrix01.print(fo);
	}
	if (filetype==BINARY) {
		matrix01.write(fo);
	}
	/////////
	if (filetype==TEXT) {
		if (layerc._size>0) {
			fprintf(fo, "\n\nWeights 1->c:\n");
			matrix12.print(fo);
    	
			fprintf(fo, "\n\nWeights c->2:\n");
			matrixc2.print(fo);
		}
		else
		{
			fprintf(fo, "\n\nWeights 1->2:\n");
			matrix12.print(fo);
		}
	}
	if (filetype==BINARY) {
		if (layerc._size>0) {
			matrix12.write(fo);
    		matrixc2.write(fo);
		}
		else
		{
			matrix12.write(fo);
		}
	}
	////////
	if (filetype==TEXT) {
		fprintf(fo, "\nDirect connections:\n");
		direct.print(fo);
	}
	if (filetype==BINARY) {
		direct.write(fo);
	}

	fclose(fo);
    
	rename(str, rnnlm_file);
}

void CRnnLM::goToDelimiter(int delim, FILE *fi)
{
	int ch=0;

	while (ch!=delim) {
		ch=fgetc(fi);
		if (feof(fi)) {
			printf("Unexpected end of file\n");
			exit(1);
		}
	}
}

void CRnnLM::restoreNet()    //will read whole network structure
{
	FILE *fi;
	int ver;
	char str[MAX_STRING];
	double d;

	fi=fopen(rnnlm_file, "rb");
	if (fi==NULL) {
		printf("ERROR: model file '%s' not found!\n", rnnlm_file);
		exit(1);
	}

	goToDelimiter(':', fi);
	fscanf(fi, "%d", &ver);
	if ((ver==4) && (version==5)) /* we will solve this later.. */ ; else
	if (ver!=version) {
		printf("Unknown version of file %s\n", rnnlm_file);
		exit(1);
	}
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &filetype);
	//
	goToDelimiter(':', fi);
	if (train_file_set==0) {
		fscanf(fi, "%s", train_file);
	} else fscanf(fi, "%s", str);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%s", valid_file);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%lf", &llogp);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &iter);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &train_cur_pos);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%lf", &logp);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &anti_k);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &train_words);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &layer0._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &layer1._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &layerc._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &layer2._size);
	//
	if (ver>5) {
		goToDelimiter(':', fi);
		fscanf(fi, "%lld", &direct._size);
	}
	//
	if (ver>6) {
		goToDelimiter(':', fi);
		fscanf(fi, "%d", &direct._order);
	}
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &bp._bptt);
	//
	if (ver>4) {
		goToDelimiter(':', fi);
		fscanf(fi, "%d", &bp._block);
	} else bp._block=10;
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &vocab._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &wordClass._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &old_classes);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &independent);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%lf", &d);
	starting_alpha=d;
	//
	goToDelimiter(':', fi);
	if (alpha_set==0) {
		fscanf(fi, "%lf", &d);
		alpha=d;
	} else fscanf(fi, "%lf", &d);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &alpha_divide);

	vocab.grow();
	//
	goToDelimiter(':', fi);
	vocab.scan(fi);
	//
	if (layer0._neurons ==NULL) initialize();		//memory allocation here
	//
    
    
	if (filetype==TEXT) {
		goToDelimiter(':', fi);
		layer1.scan(fi);
	}
	if (filetype==BINARY) {
		fgetc(fi);
		layer1.read(fi);
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);
		matrix01.scan(fi);
	}
	if (filetype==BINARY) {
		matrix01.read(fi);
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);
		if (layerc._size==0) {	//no compress layer
			matrix12.scan(fi);
		}
		else
		{
			matrix12.scan(fi);
			   	
			goToDelimiter(':', fi);

			matrixc2.scan(fi);
		}
	}
	if (filetype==BINARY) {
		if (layerc._size==0) {	//no compress layer
			matrix12.read(fi);
		}
		else
		{				//with compress layer
			matrix12.read(fi);
			matrixc2.read(fi);
		}
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);		//direct conenctions
		direct.scan(fi);
	}
	//
	if (filetype==BINARY) {
		direct.read(fi);
	}
	//
    
	saveWeights();

	fclose(fi);
}

void CRnnLM::inputLayer_clear(Neuron neurons[], int layer_size, int num_state_neurons) {
	for (int a = 0; a < (layer_size - num_state_neurons); a++)
		neurons[a].clear();
	for (int a = layer_size - num_state_neurons; a < layer_size; a++) {   //last hidden layer is initialized to vector of 0.1 values to prevent unstability
		neurons[a].ac=0.1;
		neurons[a].er=0;
	}
}

void CRnnLM::netFlush()   //cleans all activations and error vectors
{
	inputLayer_clear(layer0._neurons, layer0._size, layer1._size);
	layer1.clear();
	layerc.clear();
	layer2.clear();
}

void CRnnLM::netReset()   //cleans hidden layer activation + bptt history
{
	layer1.setActivation(1.0);

	copyHiddenLayerToInput();

	bp.reset();

	direct.clearHistory();
}

// Propagate activation from the source layer to the destination layer
// using the specified weight matrix.
void CRnnLM::matrixXvector(
	Layer &dest, 
	Layer &src, 
	Matrix &matrix, 
	int matrix_width, 
	int from, 
	int to, 
	int from2, 
	int to2, 
	int type
)
{
	int a, b;
	real val[8];
    
	if (type==0) {		//ac mod
		for (b=0; b<(to-from)/(sizeof(val) / sizeof(real)); b++) {
			for (int c = 0; c < sizeof(val) / sizeof(real); c++)
				val[c] = 0;
	    
			for (a=from2; a<to2; a++)
				for (int c = 0; c < sizeof(val) / sizeof(real); c++)
					dest._neurons[b*(sizeof(val) / sizeof(real))+from+c].ac += src._neurons[a].ac * matrix._synapses[a+(b*(sizeof(val) / sizeof(real))+from+c)*matrix_width].weight;
		}
    
		for (b=b*(sizeof(val) / sizeof(real)); b<to-from; b++) {
			for (a=from2; a<to2; a++) {
				dest._neurons[b+from].ac += src._neurons[a].ac * matrix._synapses[a+(b+from)*matrix_width].weight;
			}
		}
	}
	else {		//er mod
		for (a=0; a<(to2-from2)/(sizeof(val) / sizeof(real)); a++) {
			for (int c = 0; c < sizeof(val) / sizeof(real); c++)
				val[c] = 0;
	    
			for (b=from; b<to; b++)
				for (int c = 0; c < (sizeof(val) / sizeof(real)); c++)
					val[c] += src._neurons[b].er * matrix._synapses[a*(sizeof(val) / sizeof(real))+from2+c+b*matrix_width].weight;
			for (int c = 0; (c < sizeof(val) / sizeof(real)); c++)
				dest._neurons[a*(sizeof(val) / sizeof(real))+from2+c].er += val[c];
		}
	
		for (a=a*(sizeof(val) / sizeof(real)); a<to2-from2; a++) {
			for (b=from; b<to; b++) {
				dest._neurons[a+from2].er += src._neurons[b].er * matrix._synapses[a+from2+b*matrix_width].weight;
			}
		}
    	
		if (gradient_cutoff>0)
		for (a=from2; a<to2; a++) {
			if (dest._neurons[a].er>gradient_cutoff) dest._neurons[a].er=gradient_cutoff;
			if (dest._neurons[a].er<-gradient_cutoff) dest._neurons[a].er=-gradient_cutoff;
		}
	}
}
void CRnnLM::slowMatrixXvector(
	Neuron *dest, 
	Neuron *srcvec, 
	Synapse *srcmatrix, 
	int matrix_width, 
	int from, 
	int to, 
	int from2, 
	int to2, 
	int type
)
{
	int a, b;
    if (type==0) {		//ac mod
		for (b=from; b<to; b++) {
			for (a=from2; a<to2; a++) {
				dest[b].ac += srcvec[a].ac * srcmatrix[a+b*matrix_width].weight;
			}
		}
	}
	else if (type==1) { // er mod
		for (a=from2; a<to2; a++) {
			for (b=from; b<to; b++) {
				dest[a].er += srcvec[b].er * srcmatrix[a+b*matrix_width].weight;
			}
		}
    	
		if (gradient_cutoff>0)
		for (a=from2; a<to2; a++) {
			if (dest[a].er>gradient_cutoff) dest[a].er=gradient_cutoff;
			if (dest[a].er<-gradient_cutoff) dest[a].er=-gradient_cutoff;
		}
	}
}

void CRnnLM::clearClassActivation(int word)
{
	for (int c = 0; c < wordClass._word_count[vocab._words[word].class_index]; c++)
		layer2._neurons[wordClass._words[vocab._words[word].class_index][c]].ac = 0;
}

void CRnnLM::computeProbDist(int last_word, int word)
{
	//propagate 0->1
	layer1.clearActivation();
	
	// Propagate activation of portion from input layer storing prior hidden layer to full current hidden layer,
	// using weight matrix
	matrixXvector(layer1, layer0, matrix01, matrix01._rows, 0, layer1._size, layer0._size - layer1._size, layer0._size, 0);

	// Propagate activation of last word (only) into current hidden layer
	if (last_word != -1) {
		layer0._neurons[last_word].ac = 1;
		layer1.receiveActivation(layer0, last_word, matrix01._synapses);
	}
	//activate 1      --sigmoid
    layer1.applySigmoid();
	
	if (layerc._size>0) {
		layerc.clearActivation();
		// Propagate activation of current hidden layer into current compression layer
		matrixXvector(layerc, layer1, matrix12, matrix12._rows, 0, layerc._size, 0, layer1._size, 0);
		//activate compression      --sigmoid
		layerc.applySigmoid();
		//1->2 class
		layer2.clearActivationRange(vocab._size, layer2._size);
		// Propagate activation of current compression layer into class portion of output layer
		matrixXvector(layer2, layerc, matrixc2, matrixc2._rows, vocab._size, layer2._size, 0, layerc._size, 0);
	} else {
		//1->2 class
		layer2.clearActivationRange(vocab._size, layer2._size);
		// Propagate activation of layer 1 into class portion of output layer
		matrixXvector(layer2, layer1, matrix12, matrix12._rows, vocab._size, layer2._size, 0, layer1._size, 0);
	}

	direct.applyToClasses(layer2._neurons, vocab, layer2._size);

	layer2.normalizeActivation(vocab._size);
 
	if (gen > 0)
		return;	//if we generate words, we don't know what current word is -> only classes are estimated and word is selected in testGen()

	//1->2 word
    
	if (word != -1) {
		clearClassActivation(word);
		if (layerc._size > 0) {
			// Propagate activation of compression layer into words portion of output layer
			matrixXvector(
				layer2,
				layerc,
				matrixc2,
				matrixc2._rows,
				wordClass.firstWordInClass(vocab._words[word].class_index),
				wordClass.lastWordInClass(vocab._words[word].class_index),
				0, 
				layerc._size, 
				0
			);
		}
		else
		{
			// Propagate activation of layer1 into words portion of output layer
			matrixXvector(
				layer2,
				layer1,
				matrix12,
				matrix12._rows,
				wordClass.firstWordInClass(vocab._words[word].class_index),
				wordClass.lastWordInClass(vocab._words[word].class_index),
				0,
				layer1._size,
				0
			);
		}

		direct.applyToWords(layer2._neurons, vocab._words[word].class_index, wordClass);

		layer2.setSigmoidActivation(wordClass, vocab._words[word]);
	}
}

void CRnnLM::setOutputErrors(int word)
{
	for (int c = 0; c < wordClass._word_count[vocab._words[word].class_index]; c++) {
		layer2._neurons[wordClass._words[vocab._words[word].class_index][c]].er = (0 - layer2._neurons[wordClass._words[vocab._words[word].class_index][c]].ac);
	}
	layer2._neurons[word].er = (1-layer2._neurons[word].ac);	//word part

	for (int a = vocab._size; a < layer2._size; a++) {
		layer2._neurons[a].er = (0 - layer2._neurons[a].ac);
	}
	layer2._neurons[vocab._size + vocab._words[word].class_index].er = (1 - layer2._neurons[vocab._size + vocab._words[word].class_index].ac);	//class part
}

void CRnnLM::adjustWeights(int counter, int b, int t, real beta2)
{
	if ((counter % 10) == 0)	//regularization is done every 10 steps
		for (int a = 0; a < layer1._size; a++) 
			matrix12._synapses[a + t].weight += alpha * layer2._neurons[b].er * layer1._neurons[a].ac - matrix12._synapses[a + t].weight * beta2;
	else
		for (int a = 0; a < layer1._size; a++) 
			matrix12._synapses[a + t].weight += alpha * layer2._neurons[b].er * layer1._neurons[a].ac;
}

void CRnnLM::learn(int last_word, int word)
{
	int a, b, c, t, step;
	real beta2, beta3;

	beta2 = beta * alpha;
	beta3 = beta2 * 1;	//beta3 can be possibly larger than beta2, as that is useful on small datasets (if the final model is to be interpolated wich backoff model) - todo in the future

	if (word == -1) return;

	setOutputErrors(word);

	//flush error
	layer1.clearError();
	layerc.clearError();
    
	direct.learnForWords(word, alpha, beta3, vocab, wordClass, layer2);
	direct.learnForClasses(word, alpha, beta3, vocab, layer2);
    
	if (layerc._size>0) {
		// propagate errors from words portion of layer 2 into compression layer
		matrixXvector(
			layerc,
			layer2,
			matrixc2,
			matrixc2._columns,
			wordClass.firstWordInClass(vocab._words[word].class_index),
			wordClass.lastWordInClass(vocab._words[word].class_index),
			0,
			layerc._size,
			1
		);
	
		t = wordClass.firstWordInClass(vocab._words[word].class_index) * layerc._size;
		for (c = 0; c < wordClass.firstWordInClass(vocab._words[word].class_index); c++) {
			b = wordClass._words[vocab._words[word].class_index][c];
			adjustWeights(counter, b, t, beta2);
			t += layerc._size;
		}

		// propagate errors from classes portion of layer 2 into compression layer
		matrixXvector(
			layerc,
			layer2,
			matrixc2,
			matrixc2._columns,
			vocab._size,
			layer2._size,
			0, 
			layerc._size,
			1
		);
	
		t = vocab._size * layerc._size;
		for (b = vocab._size; b < layer2._size; b++) {
			adjustWeights(counter, b, t, beta2);
			t += layerc._size;
		}
	
		layerc.deriveError();
	
		// propagate errors from compression layer into layer 1
		matrixXvector(
			layer1,
			layerc,
			matrix12,
			matrix12._columns,
			0,
			layerc._size,
			0,
			layer1._size,
			1
		);
	
		matrix12.adjustWeights(alpha, layer1._neurons, layerc._neurons);
	}
	else
	{
		// propagate errors from portion of output layer to layer 1 
		matrixXvector(
			layer1,
			layer2,
			matrix12,
			matrix12._rows,
			wordClass._words[vocab._words[word].class_index][0],
			wordClass._words[vocab._words[word].class_index][0] + wordClass._word_count[vocab._words[word].class_index],
			0,
			layer1._size,
			1
		);
    	
		t = wordClass._words[vocab._words[word].class_index][0] * layer1._size;
		for (c = 0; c < wordClass._word_count[vocab._words[word].class_index]; c++) {
			b = wordClass._words[vocab._words[word].class_index][c];
			adjustWeights(counter, b, t, beta2);
			t += layer1._size;
		}

		// propagate errors from classes portion of output layer to layer 1
		matrixXvector(
			layer1,
			layer2,
			matrix12,
			matrix12._rows,
			vocab._size,
			layer2._size,
			0,
			layer1._size,
			1
		);
	
		t = vocab._size * layer1._size;
		for (b = vocab._size; b < layer2._size; b++) {
			adjustWeights(counter, b, t, beta2);
			t += layer1._size;
		}
	}

	if (bp._bptt <= 1) {		//bptt==1 -> normal BP
		layer1.deriveError();

		//weight update 1->0
		a=last_word;
		if (a!=-1) {
			if ((counter % 10) == 0)
				matrix01.adjustWeightsBeta2(a, alpha, beta2, layer0._neurons, layer1._neurons);
			else
				matrix01.adjustRowWeights(a, alpha, layer0._neurons, layer1._neurons);
		}

		if ((counter % 10) == 0) {
			for (a = layer0._size - layer1._size; a < layer0._size; a++)
				matrix01.adjustWeightsBeta2(a, alpha, beta2, layer0._neurons, layer1._neurons);
		} else {
			for (a = layer0._size - layer1._size; a < layer0._size; a++) 
				matrix01.adjustRowWeights(a, alpha, layer0._neurons, layer1._neurons);
		}
	}
	else		//BPTT
	{
		for (b = 0; b < layer1._size; b++) 
			bp._neurons[b].copy(layer1._neurons[b]);
	
		if (((counter % bp._block) == 0) || (independent && (word == 0))) {
			for (step = 0; step < bp._bptt + bp._block - 2; step++) {
				layer1.deriveError();

				//weight update 1->0
				a = bp.wordFromPast(step);
				if (a != -1)
					bp.adjustRowWeights(a, alpha, 1.0, layer1._neurons);

				for (a = layer0._size - layer1._size; a < layer0._size; a++)
					layer0._neurons[a].er = 0;

				// propagate errors from entire layer 1 to prior-state portion of input layer
				matrixXvector(
					layer0,
					layer1,
					matrix01,
					matrix01._rows,
					0,
					layer1._size,
					layer0._size-layer1._size,
					layer0._size,
					1
				);

				for (a = layer0._size - layer1._size; a < layer0._size; a++) {
					bp.adjustRowWeights(a, alpha, layer0._neurons[a].ac, layer1._neurons);
				}
	    
				for (a=0; a<layer1._size; a++) {		//propagate error from time T-n to T-n-1
					layer1._neurons[a].er = layer0._neurons[a + layer0._size - layer1._size].er + bp._neurons[(step + 1) * layer1._size + a].er;
				}
	    
				if (step < bp._bptt + bp._block - 3)
				for (a = 0; a < layer1._size; a++) {
					layer1._neurons[a].ac = bp._neurons[(step + 1) * layer1._size + a].ac;
					layer0._neurons[a + layer0._size - layer1._size].ac = bp._neurons[(step + 2) * layer1._size + a].ac;
				}
			}
	    
			for (a=0; a<(bp._bptt+bp._block)*layer1._size; a++) {
				bp._neurons[a].er=0;
			}
		
			for (b=0; b<layer1._size; b++) layer1._neurons[b].ac=bp._neurons[b].ac;		//restore hidden layer after bptt

			//
			for (b=0; b<layer1._size; b++) {		//copy temporary syn0
				if ((counter%10)==0) {
					for (a=layer0._size-layer1._size; a<layer0._size; a++) {
						matrix01._synapses[a+b*layer0._size].weight+=bp._synapses[a+b*layer0._size].weight - matrix01._synapses[a+b*layer0._size].weight*beta2;
						bp._synapses[a+b*layer0._size].weight=0;
					}
				}
				else {
					for (a=layer0._size-layer1._size; a<layer0._size; a++) {
						matrix01._synapses[a+b*layer0._size].weight+=bp._synapses[a+b*layer0._size].weight;
						bp._synapses[a+b*layer0._size].weight=0;
					}
				}
	    
				if ((counter % 10)==0) {
					for (step=0; step<bp._bptt+bp._block-2; step++) if (bp.wordFromPast(step)!=-1) {
						matrix01._synapses[bp.wordFromPast(step)+b*layer0._size].weight+=bp._synapses[bp.wordFromPast(step)+b*layer0._size].weight - matrix01._synapses[bp.wordFromPast(step)+b*layer0._size].weight*beta2;
						bp._synapses[bp.wordFromPast(step)+b*layer0._size].weight=0;
					}
				}
				else {
					for (step=0; step<bp._bptt+bp._block-2; step++) if (bp.wordFromPast(step)!=-1) {
						matrix01._synapses[bp.wordFromPast(step)+b*layer0._size].weight+=bp._synapses[bp.wordFromPast(step)+b*layer0._size].weight;
						bp._synapses[bp.wordFromPast(step)+b*layer0._size].weight=0;
					}
				}
			}
		}
	}	
}

void CRnnLM::copyHiddenLayerToInput()
{
	int a;

	for (a=0; a<layer1._size; a++) {
		layer0._neurons[a+layer0._size-layer1._size].ac=layer1._neurons[a].ac;
	}
}

void CRnnLM::trainNet()
{
	int a, word, last_word, wordcn;
	char log_name[200];
	FILE *fi, *flog;
	clock_t start, now;

	sprintf(log_name, "%s.output.txt", rnnlm_file);

	printf("Starting training using file %s\n", train_file);
	starting_alpha=alpha;
    
	fi=fopen(rnnlm_file, "rb");
	if (fi!=NULL) {
		fclose(fi);
		printf("Restoring network from file to continue training...\n");
		restoreNet();
	} else {
		train_words = vocab.learnFromTrainFile(train_file, debug_mode);
		initialize();
		iter=0;
	}

	if (wordClass._size>vocab._size) {
		printf("WARNING: number of classes exceeds vocabulary size!\n");
	}
    
	counter=train_cur_pos;
	//saveNet();
	while (iter < maxIter) {
		printf("Iter: %3d\tAlpha: %f\t   ", iter, alpha);
		fflush(stdout);
        
		bp.clearHistory();
		direct.clearHistory();

		//TRAINING PHASE
		netFlush();

		fi=fopen(train_file, "rb");
		last_word=0;
        
		if (counter>0) 
			for (a=0; a<counter; a++) 
				(void) readWordIndex(fi);	//this will skip words that were already learned if the training was interrupted
        
		start=clock();
        
		while (1) {
			counter++;
    	    
			if ((counter%10000)==0) if ((debug_mode>1)) {
				now=clock();
				if (train_words>0)
					printf("%cIter: %3d\tAlpha: %f\t   TRAIN entropy: %.4f    Progress: %.2f%%   Words/sec: %.1f ", 13, iter, alpha, -logp/log10(2)/counter, counter/(real)train_words*100, counter/((double)(now-start)/1000000.0));
				else
					printf("%cIter: %3d\tAlpha: %f\t   TRAIN entropy: %.4f    Progress: %dK", 13, iter, alpha, -logp/log10(2)/counter, counter/1000);
				fflush(stdout);
			}
    	    
			if ((anti_k>0) && ((counter%anti_k)==0)) {
				train_cur_pos=counter;
				saveNet();
			}
        
			word = readWordIndex(fi);     //read next word
			computeProbDist(last_word, word);
			if (feof(fi)) break;        //end of file: test on validation data, iterate till convergence

			if (word != -1) 
				logp += log10(layer2._neurons[vocab._size + vocab._words[word].class_index].ac * layer2._neurons[word].ac);
    	    
			if ((logp != logp) || (isinf(logp))) {
				printf("\nNumerical error %d %f %f\n", word, layer2._neurons[word].ac, layer2._neurons[vocab._words[word].class_index + vocab._size].ac);
				exit(1);
			}

			bp.shift(last_word);

			learn(last_word, word);
            
			copyHiddenLayerToInput();

			if (last_word != -1) 
				layer0._neurons[last_word].ac = 0;  //delete previous activation

			last_word = word;
            
			direct.push(last_word);

			if (independent && (word==0)) netReset();
		}
		fclose(fi);

		now=clock();
		printf("%cIter: %3d\tAlpha: %f\t   TRAIN entropy: %.4f    Words/sec: %.1f   ", 13, iter, alpha, -logp/log10(2)/counter, counter/((double)(now-start)/1000000.0));
   
		if (one_iter==1) {	//no validation data are needed and network is always saved with modified weights
			printf("\n");
			logp=0;
			saveNet();
			break;
		}

		//VALIDATION PHASE
		netFlush();

		fi=fopen(valid_file, "rb");
		if (fi==NULL) {
			printf("Valid file not found\n");
			exit(1);
		}
        
		flog=fopen(log_name, "ab");
		if (flog==NULL) {
			printf("Cannot open log file\n");
			exit(1);
		}
        
		//fprintf(flog, "Index   P(NET)          Word\n");
		//fprintf(flog, "----------------------------------\n");
        
		last_word=0;
		logp=0;
		wordcn=0;
		while (1) {
			word=readWordIndex(fi);     //read next word
			computeProbDist(last_word, word);      //compute probability distribution
			if (feof(fi)) break;        //end of file: report LOGP, PPL
            
			if (word!=-1) {
				logp+=log10(layer2._neurons[vocab._words[word].class_index+vocab._size].ac * layer2._neurons[word].ac);
				wordcn++;
			}

			copyHiddenLayerToInput();

			if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation

			last_word=word;
            direct.push(last_word);

			if (independent && (word==0)) netReset();
		}
		fclose(fi);
        
		fprintf(flog, "\niter: %d\n", iter);
		fprintf(flog, "valid log probability: %f\n", logp);
		fprintf(flog, "PPL net: %f\n", pow(10.0, -logp/(real)wordcn));
        
		fclose(flog);
    
		printf("VALID entropy: %.4f\n", -logp/log10(2)/wordcn);
        
		counter=0;
		train_cur_pos=0;

		if (logp<llogp)
			restoreWeights();
		else
			saveWeights();

		if (logp*min_improvement<llogp) {
			if (alpha_divide==0) alpha_divide=1;
			else {
				saveNet();
				break;
			}
		}

		if (alpha_divide) alpha/=2;

		llogp=logp;
		logp=0;
		iter++;
		saveNet();
	}
}

void CRnnLM::testNet()
{
	int word, last_word, wordcn;
	FILE *fi, *flog, *lmprob=NULL;
	real prob_other, log_other, log_combine;
	double d;
    
	restoreNet();
    
	if (use_lmprob) {
		lmprob=fopen(lmprob_file, "rb");
	}

	//TEST PHASE
	//netFlush();

	fi=fopen(test_file, "rb");
	//sprintf(str, "%s.%s.output.txt", rnnlm_file, test_file);
	//flog=fopen(str, "wb");
	flog=stdout;

	if (debug_mode>1)	{
		if (use_lmprob) {
			fprintf(flog, "Index   P(NET)          P(LM)           Word\n");
			fprintf(flog, "--------------------------------------------------\n");
		} else {
			fprintf(flog, "Index   P(NET)          Word\n");
			fprintf(flog, "----------------------------------\n");
		}
	}

	last_word=0;					//last word = end of sentence
	logp=0;
	log_other=0;
	log_combine=0;
	prob_other=0;
	wordcn=0;
	copyHiddenLayerToInput();
    
	bp.clearHistory();
	direct.clearHistory();

	if (independent) netReset();
    
	while (1) {
        
		word=readWordIndex(fi);		//read next word
		computeProbDist(last_word, word);		//compute probability distribution
		if (feof(fi)) break;		//end of file: report LOGP, PPL
        
		if (use_lmprob) {
			fscanf(lmprob, "%lf", &d);
			prob_other=d;

			goToDelimiter('\n', lmprob);
		}

		if ((word!=-1) || (prob_other>0)) {
			if (word==-1) {
				logp+=-8;		//some ad hoc penalty - when mixing different vocabularies, single model score is not real PPL
				log_combine+=log10(0 * lambda + prob_other*(1-lambda));
			} else {
				logp+=log10(layer2._neurons[vocab._words[word].class_index+vocab._size].ac * layer2._neurons[word].ac);
				log_combine+=log10(layer2._neurons[vocab._words[word].class_index+vocab._size].ac * layer2._neurons[word].ac*lambda + prob_other*(1-lambda));
			}
			log_other+=log10(prob_other);
			wordcn++;
		}

		if (debug_mode>1) {
			if (use_lmprob) {
				if (word!=-1) fprintf(flog, "%d\t%.10f\t%.10f\t%s", word, layer2._neurons[vocab._words[word].class_index+vocab._size].ac *layer2._neurons[word].ac, prob_other, vocab._words[word].word);
				else fprintf(flog, "-1\t0\t\t0\t\tOOV");
			} else {
				if (word!=-1) fprintf(flog, "%d\t%.10f\t%s", word, layer2._neurons[vocab._words[word].class_index+vocab._size].ac *layer2._neurons[word].ac, vocab._words[word].word);
				else fprintf(flog, "-1\t0\t\tOOV");
			}
    	    
			fprintf(flog, "\n");
		}

		if (dynamic > 0) {
			bp.shift(last_word);
			alpha=dynamic;
			learn(last_word, word);    //dynamic update
		}
		copyHiddenLayerToInput();
        
		if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation

		last_word=word;
        direct.push(last_word);

		if (independent && (word==0)) netReset();
	}
	fclose(fi);
	if (use_lmprob) fclose(lmprob);

	//write to log file
	if (debug_mode>0) {
		fprintf(flog, "\ntest log probability: %f\n", logp);
		if (use_lmprob) {
			fprintf(flog, "test log probability given by other lm: %f\n", log_other);
			fprintf(flog, "test log probability %f*rnn + %f*other_lm: %f\n", lambda, 1-lambda, log_combine);
		}

		fprintf(flog, "\nPPL net: %f\n", pow(10.0, -logp/(real)wordcn));
		if (use_lmprob) {
			fprintf(flog, "PPL other: %f\n", pow(10.0, -log_other/(real)wordcn));
			fprintf(flog, "PPL combine: %f\n", pow(10.0, -log_combine/(real)wordcn));
		}
	}
    
	fclose(flog);
}

void CRnnLM::testNbest()
{
	int word, last_word, wordcn;
	FILE *fi, *flog, *lmprob=NULL;
	float prob_other; //has to be float so that %f works in fscanf
	real log_other, log_combine, senp;
	//int nbest=-1;
	int nbest_cn=0;
	char ut1[MAX_STRING], ut2[MAX_STRING];

	restoreNet();
	computeProbDist(0, 0);
	copyHiddenLayerToInput();
	layer1.backup(0);
	layer1.backup(1);
    
	if (use_lmprob) {
		lmprob=fopen(lmprob_file, "rb");
	} else lambda=1;		//!!! for simpler implementation later

	//TEST PHASE
	//netFlush();
    direct.clearHistory();

	if (!strcmp(test_file, "-")) fi=stdin; else fi=fopen(test_file, "rb");
    
	//sprintf(str, "%s.%s.output.txt", rnnlm_file, test_file);
	//flog=fopen(str, "wb");
	flog=stdout;

	last_word=0;		//last word = end of sentence
	logp=0;
	log_other=0;
	prob_other=0;
	log_combine=0;
	wordcn=0;
	senp=0;
	strcpy(ut1, (char *)"");
	while (1) {
		if (last_word==0) {
			fscanf(fi, "%s", ut2);
	    
			if (nbest_cn==1) 
				layer1.backup(1); //save context after processing first sentence in nbest
	    
			if (strcmp(ut1, ut2)) {
				strcpy(ut1, ut2);
				nbest_cn=0;
				layer1.restore(1);
				layer1.backup(0);
			} else 
				layer1.restore(0);
	    
			nbest_cn++;
	    
			copyHiddenLayerToInput();
		}
    
	
		word=readWordIndex(fi);     //read next word
		if (lambda>0) computeProbDist(last_word, word);      //compute probability distribution
		if (feof(fi)) break;        //end of file: report LOGP, PPL
        
        
		if (use_lmprob) {
			fscanf(lmprob, "%f", &prob_other);
			goToDelimiter('\n', lmprob);
		}
        
		if (word!=-1)
			layer2._neurons[word].ac*=layer2._neurons[vocab._words[word].class_index+vocab._size].ac;
        
		if (word!=-1) {
			logp+=log10(layer2._neurons[word].ac);
    	    
			log_other+=log10(prob_other);
            
			log_combine+=log10(layer2._neurons[word].ac*lambda + prob_other*(1-lambda));
            
			senp+=log10(layer2._neurons[word].ac*lambda + prob_other*(1-lambda));
            
			wordcn++;
		} else {
			//assign to OOVs some score to correctly rescore nbest lists, reasonable value can be less than 1/|V| or backoff LM score (in case it is trained on more data)
			//this means that PPL results from nbest list rescoring are not true probabilities anymore (as in open vocabulary LMs)
    	    
			real oov_penalty=-5;	//log penalty
    	    
			if (prob_other!=0) {
				logp+=log10(prob_other);
				log_other+=log10(prob_other);
				log_combine+=log10(prob_other);
				senp+=log10(prob_other);
			} else {
				logp+=oov_penalty;
				log_other+=oov_penalty;
				log_combine+=oov_penalty;
				senp+=oov_penalty;
			}
			wordcn++;
		}
        
		//learn(last_word, word);    //*** this will be in implemented for dynamic models
		copyHiddenLayerToInput();

		if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation
        
		if (word==0) {		//write last sentence log probability / likelihood
			fprintf(flog, "%f\n", senp);
			senp=0;
		}

		last_word=word;
        direct.push(last_word);

		if (independent && (word==0)) netReset();
	}
	fclose(fi);
	if (use_lmprob) fclose(lmprob);

	if (debug_mode>0) {
		printf("\ntest log probability: %f\n", logp);
		if (use_lmprob) {
			printf("test log probability given by other lm: %f\n", log_other);
			printf("test log probability %f*rnn + %f*other_lm: %f\n", lambda, 1-lambda, log_combine);
		}

		printf("\nPPL net: %f\n", pow(10.0, -logp/(real)wordcn));
		if (use_lmprob) {
			printf("PPL other: %f\n", pow(10.0, -log_other/(real)wordcn));
			printf("PPL combine: %f\n", pow(10.0, -log_combine/(real)wordcn));
		}
	}

	fclose(flog);
}

void CRnnLM::testGen()
{
	int i, word, cla, last_word, wordcn, c, a=0;
	real f, g, sum;
    
	restoreNet();
    
	word=0;
	last_word=0;					//last word = end of sentence
	wordcn=0;
	copyHiddenLayerToInput();
	while (wordcn<gen) {
		computeProbDist(last_word, 0);		//compute probability distribution
        
		f = Matrix::random(0, 1);
		g=0;
		i=vocab._size;
		while ((g<f) && (i<layer2._size)) {
			g+=layer2._neurons[i].ac;
			i++;
		}
		cla=i-1-vocab._size;
        
		if (cla>wordClass._size-1) cla=wordClass._size-1;
		if (cla<0) cla=0;
        
		//
		// !!!!!!!!  THIS WILL WORK ONLY IF CLASSES ARE CONTINUALLY DEFINED IN VOCAB !!! (like class 10 = words 11 12 13; not 11 12 16)  !!!!!!!!
		// forward pass 1->2 for words
		for (c=0; c<wordClass._word_count[cla]; c++) layer2._neurons[wordClass._words[cla][c]].ac=0;
		
		// propagate activation from layer1 to portion of layer 2
		matrixXvector(
			layer2,
			layer1,
			matrix12,
			matrix12._rows,
			wordClass._words[cla][0],
			wordClass._words[cla][0] + wordClass._word_count[cla],
			0,
			layer1._size,
			0
		);
        
		//apply direct connections to words
		if (word!=-1) 
			direct.applyToWords(layer2._neurons, cla, wordClass);
        
		//activation 2   --softmax on words
		// 130425 - this is now a 'safe' softmax

		sum=0;
		real maxAc=-FLT_MAX;
		for (c=0; c<wordClass._word_count[cla]; c++) {
			a=wordClass._words[cla][c];
			if (layer2._neurons[a].ac>maxAc) maxAc=layer2._neurons[a].ac;
		}
		for (c=0; c<wordClass._word_count[cla]; c++) {
			a=wordClass._words[cla][c];
			sum+=fasterexp(layer2._neurons[a].ac-maxAc);
		}
		for (c=0; c<wordClass._word_count[cla]; c++) {
			a=wordClass._words[cla][c];
			layer2._neurons[a].ac=fasterexp(layer2._neurons[a].ac-maxAc)/sum; //this prevents the need to check for overflow
		}
		//
	
		f = Matrix::random(0, 1);
		g=0;
		/*i=0;
		while ((g<f) && (i<vocab._size)) {
		g+=layer2._neurons[i].ac;
		i++;
		}*/
		for (c=0; c<wordClass._word_count[cla]; c++) {
			a=wordClass._words[cla][c];
			g+=layer2._neurons[a].ac;
			if (g>f) break;
		}
		word=a;
        
		if (word>vocab._size-1) word=vocab._size-1;
		if (word<0) word=0;

		//printf("%s %d %d\n", vocab._words[word].word, cla, word);
		if (word!=0)
			printf("%s ", vocab._words[word].word);
		else
			printf("\n");

		copyHiddenLayerToInput();

		if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation

		last_word=word;
        direct.push(last_word);

		if (independent && (word==0)) netReset();
        
		wordcn++;
	}
}
