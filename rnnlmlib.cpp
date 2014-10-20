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
#include "rnnlmlib.h"

///// include blas
#ifdef USE_BLAS
extern "C" {
#include <cblas.h>
}
#endif
//


real CRnnLM::random(real min, real max)
{
	return rand()/(real)RAND_MAX*(max-min)+min;
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





int CRnnLM::readWordIndex(FILE *fin)
{
	char word[MAX_STRING];

	vocab.readWord(word, fin);
	if (feof(fin)) return -1;

	return vocab.search(word);
}

void CRnnLM::layer_copy_layer(Neuron neu0b[], int layer_size, Neuron neu0[]) {
	for (int a=0; a<layer_size; a++) {
		neu0b[a].copy(neu0[a]);
	}
}

void CRnnLM::matrix_copy_matrix(Synapse dest[], Synapse src[], int rows, int columns) {
	for (int b = 0; b < rows; b++)
		for (int a = 0; a < columns; a++) 
			dest[a + b * columns].weight = src[a + b * columns].weight;	
}

void CRnnLM::saveWeights()      //saves current weights and unit activations
{
	layer_copy_layer(neu0b, layer0._size, layer0._neurons);
	layer_copy_layer(neu1b, layer1._size, layer1._neurons);
    layer_copy_layer(neucb, layerc._size, layerc._neurons);
	layer_copy_layer(neu2b, layer2._size, layer2._neurons);
    
	matrix_copy_matrix(syn0b, syn0, layer1._size, layer0._size);
    
	matrix_copy_matrix(syn1b, syn1, layerc._size, layer1._size);
	if (layerc._size>0) {
		matrix_copy_matrix(syncb, sync, layer2._size, layerc._size);
	}
}

void CRnnLM::restoreWeights()      //restores current weights and unit activations from backup copy
{
	layer_clearActivation(layer0._neurons, layer0._size);
	layer_clearError(layer0._neurons, layer0._size);

	layer_clearActivation(layer1._neurons, layer1._size);
	layer_clearError(layer1._neurons, layer1._size);

	layer_clearActivation(layerc._neurons, layerc._size);
	layer_clearError(layerc._neurons, layerc._size);

	layer_clearActivation(layer2._neurons, layer2._size);
	layer_clearError(layer2._neurons, layer2._size);

	matrix_copy_matrix(syn0, syn0b, layer1._size, layer0._size);
    
	matrix_copy_matrix(syn1, syn1b, layerc._size, layer1._size);
	if (layerc._size>0) {
		matrix_copy_matrix(sync, syncb, layer2._size, layerc._size);
	}
}

void CRnnLM::initialize()
{
	int a, b, cl;

	layer0._size = vocab._size + layer1._size;
	layer2._size = vocab._size + class_size;

	layer0._neurons = (Neuron *)calloc(layer0._size, sizeof(Neuron));
	layer1._neurons = (Neuron *)calloc(layer1._size, sizeof(Neuron));
	layerc._neurons = (Neuron *)calloc(layerc._size, sizeof(Neuron));
	layer2._neurons = (Neuron *)calloc(layer2._size, sizeof(Neuron));

	syn0 = (Synapse *)calloc(layer0._size * layer1._size, sizeof(Synapse));
	if (layerc._size==0)
		syn1=(Synapse *)calloc(layer1._size * layer2._size, sizeof(Synapse));
	else {
		syn1=(Synapse *)calloc(layer1._size * layerc._size, sizeof(Synapse));
		sync=(Synapse *)calloc(layerc._size * layer2._size, sizeof(Synapse));
	}

	if (syn1 == NULL) {
		printf("Memory allocation failed\n");
		exit(1);
	}
    
	if (layerc._size > 0) if (sync == NULL) {
		printf("Memory allocation failed\n");
		exit(1);
	}
    
	syn_d = (direct_t *)calloc((long long)direct_size, sizeof(direct_t));

	if (syn_d==NULL) {
		printf("Memory allocation for direct connections failed (requested %lld bytes)\n", (long long)direct_size * (long long)sizeof(direct_t));
		exit(1);
	}

	neu0b=(Neuron *)calloc(layer0._size, sizeof(Neuron));
	neu1b=(Neuron *)calloc(layer1._size, sizeof(Neuron));
	neucb=(Neuron *)calloc(layerc._size, sizeof(Neuron));
	neu1b2=(Neuron *)calloc(layer1._size, sizeof(Neuron));
	neu2b=(Neuron *)calloc(layer2._size, sizeof(Neuron));

	syn0b=(Synapse *)calloc(layer0._size*layer1._size, sizeof(Synapse));
	if (layerc._size==0)
		syn1b=(Synapse *)calloc(layer1._size*layer2._size, sizeof(Synapse));
	else {
		syn1b=(Synapse *)calloc(layer1._size*layerc._size, sizeof(Synapse));
		syncb=(Synapse *)calloc(layerc._size*layer2._size, sizeof(Synapse));
	}

	if (syn1b==NULL) {
		printf("Memory allocation failed\n");
		exit(1);
	}
    
	layer_clearActivation(layer0._neurons, layer0._size);
	layer_clearError(layer0._neurons, layer0._size);

	layer_clearActivation(layer1._neurons, layer1._size);
	layer_clearError(layer1._neurons, layer1._size);

	layer_clearActivation(layerc._neurons, layerc._size);
	layer_clearError(layerc._neurons, layerc._size);

	layer_clearActivation(layer2._neurons, layer2._size);
	layer_clearError(layer2._neurons, layer2._size);

	randomizeWeights(syn0, layer0._size, layer1._size);

	randomizeWeights(syn1, layer2._size, layer1._size);
	if (layerc._size>0) {
		randomizeWeights(sync, layer2._size, layerc._size);
	}
    
	long long aa;
	for (aa=0; aa<direct_size; aa++) syn_d[aa]=0;
    
	if (bptt>0) {
		bptt_history=(int *)calloc((bptt+bptt_block+10), sizeof(int));
		for (a=0; a<bptt+bptt_block; a++) bptt_history[a]=-1;
		//
		bptt_hidden=(Neuron *)calloc((bptt+bptt_block+1)*layer1._size, sizeof(Neuron));
		for (a=0; a<(bptt+bptt_block)*layer1._size; a++) {
			bptt_hidden[a].clear();
		}
		//
		bptt_syn0=(Synapse *)calloc(layer0._size*layer1._size, sizeof(Synapse));
		if (bptt_syn0==NULL) {
			printf("Memory allocation failed\n");
			exit(1);
		}
	}

	saveWeights();
    
	double df, dd;
	int i;
    
	df=0;
	dd=0;
	a=0;
	b=0;

	if (old_classes) {  	// old classes
		vocab.setClassIndexOld(class_size);
	} else {			// new classes
		vocab.setClassIndexNew(class_size);
	}
    
	//allocate auxiliary class variables (for faster search when normalizing probability at output layer)
    
	class_words=(int **)calloc(class_size, sizeof(int *));
	class_word_count=(int *)calloc(class_size, sizeof(int));
	class_max_cn=(int *)calloc(class_size, sizeof(int));
    
	for (i=0; i<class_size; i++) {
		class_word_count[i]=0;
		class_max_cn[i]=10;
		class_words[i]=(int *)calloc(class_max_cn[i], sizeof(int));
	}
    
	for (i=0; i<vocab._size; i++) {
		cl=vocab._words[i].class_index;
		class_words[cl][class_word_count[cl]]=i;
		class_word_count[cl]++;
		if (class_word_count[cl]+2>=class_max_cn[cl]) {
			class_max_cn[cl]+=10;
			class_words[cl]=(int *)realloc(class_words[cl], class_max_cn[cl]*sizeof(int));
		}
	}
}

void CRnnLM::layer_print(Neuron neurons[], int layer_size, FILE *fo) {
	for (int a = 0; a < layer_size; a++) 
		fprintf(fo, "%.4f\n", neurons[a].ac);
}

void CRnnLM::layer_write(Neuron neurons[], int layer_size, FILE *fo) {
	for (int a = 0; a < layer_size; a++) {
		float fl = neurons[a].ac;
		fwrite(&fl, sizeof(fl), 1, fo);
	}
}

void CRnnLM::matrix_print(Synapse synapses[], int rows, int columns, FILE *fo) {
	for (long long b = 0; b < rows; b++) {
		for (int a = 0; a < columns; a++) {
			synapses[a + b * columns].printWeight(fo);
		}
	}
}

void CRnnLM::matrix_write(Synapse synapses[], int rows, int columns, FILE *fo) {
	for (long long b = 0; b < rows; b++) {
		for (int a = 0; a < columns; a++) {
			syn0[a + b * columns].writeWeight(fo);
		}
	}
}

void CRnnLM::saveNet()       //will save the whole network structure                                                        
{
	FILE *fo;
	char str[1000];
	float fl;
    
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

	fprintf(fo, "direct connections: %lld\n", direct_size);
	fprintf(fo, "direct order: %d\n", direct_order);
    
	fprintf(fo, "bptt: %d\n", bptt);
	fprintf(fo, "bptt block: %d\n", bptt_block);
    
	fprintf(fo, "vocabulary size: %d\n", vocab._size);
	fprintf(fo, "class size: %d\n", class_size);
    
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
		layer_print(layer1._neurons, layer1._size, fo);
	}
	if (filetype==BINARY) {
		layer_write(layer1._neurons, layer1._size, fo);
	}
	//////////
	if (filetype==TEXT) {
		fprintf(fo, "\nWeights 0->1:\n");
		matrix_print(syn0, layer1._size, layer0._size, fo);
	}
	if (filetype==BINARY) {
		matrix_write(syn0, layer1._size, layer0._size, fo);
	}
	/////////
	if (filetype==TEXT) {
		if (layerc._size>0) {
			fprintf(fo, "\n\nWeights 1->c:\n");
			matrix_print(syn1, layerc._size, layer1._size, fo);
    	
			fprintf(fo, "\n\nWeights c->2:\n");
			matrix_print(sync, layer2._size, layerc._size, fo);
		}
		else
		{
			fprintf(fo, "\n\nWeights 1->2:\n");
			matrix_print(syn1, layer2._size, layer1._size, fo);
		}
	}
	if (filetype==BINARY) {
		if (layerc._size>0) {
			matrix_write(syn1, layerc._size, layer1._size, fo);
    		matrix_write(sync, layer2._size, layerc._size, fo);
		}
		else
		{
			matrix_write(syn1, layer1._size, layer2._size, fo);
		}
	}
	////////
	if (filetype==TEXT) {
		fprintf(fo, "\nDirect connections:\n");
		long long aa;
		for (aa=0; aa<direct_size; aa++) {
			fprintf(fo, "%.2f\n", syn_d[aa]);
		}
	}
	if (filetype==BINARY) {
		long long aa;
		for (aa=0; aa<direct_size; aa++) {
			fl=syn_d[aa];
			fwrite(&fl, sizeof(fl), 1, fo);
		}
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

void CRnnLM::layer_scan(Neuron neurons[], int layer_size, FILE *fi) {
	for (int a = 0; a < layer_size; a++)
		neurons[a].scanActivation(fi);
}

void CRnnLM::layer_read(Neuron neurons[], int layer_size, FILE *fi) {
	for (int a = 0; a < layer_size; a++) {
		neurons[a].readActivation(fi);
	}
}

void CRnnLM::matrix_scan(Synapse synapses[], int rows, int columns, FILE *fi) {
	for (int b = 0; b < rows; b++) {
		for (int a = 0; a < columns; a++) {
			synapses[a + b * columns].scanWeight(fi);
		}
	}
}

void CRnnLM::matrix_read(Synapse synapses[], int rows, int columns, FILE *fi) {
	for (int b = 0; b < rows; b++) {
		for (int a = 0; a < columns; a++) {
			synapses[a + b * columns].readWeight(fi);
		}
	}
}

void CRnnLM::restoreNet()    //will read whole network structure
{
	FILE *fi;
	int ver;
	float fl;
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
		fscanf(fi, "%lld", &direct_size);
	}
	//
	if (ver>6) {
		goToDelimiter(':', fi);
		fscanf(fi, "%d", &direct_order);
	}
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &bptt);
	//
	if (ver>4) {
		goToDelimiter(':', fi);
		fscanf(fi, "%d", &bptt_block);
	} else bptt_block=10;
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &vocab._size);
	//
	goToDelimiter(':', fi);
	fscanf(fi, "%d", &class_size);
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
		layer_scan(layer1._neurons, layer1._size, fi);
	}
	if (filetype==BINARY) {
		fgetc(fi);
		layer_read(layer1._neurons, layer1._size, fi);
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);
		matrix_scan(syn0, layer1._size, layer0._size, fi);
	}
	if (filetype==BINARY) {
		matrix_read(syn0, layer1._size, layer0._size, fi);
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);
		if (layerc._size==0) {	//no compress layer
			matrix_scan(syn1, layer2._size, layer1._size, fi);
		}
		else
		{
			matrix_scan(syn1, layerc._size, layer1._size, fi);
			   	
			goToDelimiter(':', fi);

			matrix_scan(sync, layer2._size, layerc._size, fi);
		}
	}
	if (filetype==BINARY) {
		if (layerc._size==0) {	//no compress layer
			matrix_read(syn1, layer2._size, layer1._size, fi);
		}
		else
		{				//with compress layer
			matrix_read(syn1, layerc._size, layer1._size, fi);
			matrix_read(sync, layer2._size, layerc._size, fi);
		}
	}
	//
	if (filetype==TEXT) {
		goToDelimiter(':', fi);		//direct conenctions
		long long aa;
		for (aa=0; aa<direct_size; aa++) {
			fscanf(fi, "%lf", &d);
			syn_d[aa]=d;
		}
	}
	//
	if (filetype==BINARY) {
		long long aa;
		for (aa=0; aa<direct_size; aa++) {
			fread(&fl, sizeof(fl), 1, fi);
			syn_d[aa]=fl;
		}
	}
	//
    
	saveWeights();

	fclose(fi);
}

void CRnnLM::layer_clear(Neuron neurons[], int layer_size) {
	for (int a=0; a<layer_size; a++) {
		neurons[a].clear();
	}
}

void CRnnLM::layer_setActivation(Neuron neurons[], int layer_size, real activation) {
	for (int a = 0; a < layer_size; a++)
		neurons[a].ac = 1.0;
}

void CRnnLM::inputLayer_clear(Neuron neurons[], int layer_size, int num_state_neurons) {
	layer_clear(neurons, layer_size - num_state_neurons);
	for (int a = layer_size - num_state_neurons; a < layer_size; a++) {   //last hidden layer is initialized to vector of 0.1 values to prevent unstability
		neurons[a].ac=0.1;
		neurons[a].er=0;
	}
}

void CRnnLM::netFlush()   //cleans all activations and error vectors
{
	inputLayer_clear(layer0._neurons, layer0._size, layer1._size);

	layer_clear(layer1._neurons, layer1._size);
    
	layer_clear(layerc._neurons, layerc._size);
    
	layer_clear(layer2._neurons, layer2._size);
}

void CRnnLM::netReset()   //cleans hidden layer activation + bptt history
{
	int a, b;

	layer_setActivation(layer1._neurons, layer1._size, 1.0);

	copyHiddenLayerToInput();

	if (bptt>0) {
		for (a=1; a<bptt+bptt_block; a++) bptt_history[a]=0;
		for (a=bptt+bptt_block-1; a>1; a--) for (b=0; b<layer1._size; b++) {
			bptt_hidden[a*layer1._size+b].clear();
		}
	}

	for (a=0; a<MAX_NGRAM_ORDER; a++) history[a]=0;
}

void CRnnLM::matrixXvector(
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
	real val[8];
    
	if (type==0) {		//ac mod
		for (b=0; b<(to-from)/(sizeof(val) / sizeof(real)); b++) {
			for (int c = 0; c < sizeof(val) / sizeof(real); c++)
				val[c] = 0;
	    
			for (a=from2; a<to2; a++)
				for (int c = 0; c < sizeof(val) / sizeof(real); c++)
					dest[b*(sizeof(val) / sizeof(real))+from+c].ac += srcvec[a].ac * srcmatrix[a+(b*(sizeof(val) / sizeof(real))+from+c)*matrix_width].weight;
		}
    
		for (b=b*(sizeof(val) / sizeof(real)); b<to-from; b++) {
			for (a=from2; a<to2; a++) {
				dest[b+from].ac += srcvec[a].ac * srcmatrix[a+(b+from)*matrix_width].weight;
			}
		}
	}
	else {		//er mod
		for (a=0; a<(to2-from2)/(sizeof(val) / sizeof(real)); a++) {
			for (int c = 0; c < sizeof(val) / sizeof(real); c++)
				val[c] = 0;
	    
			for (b=from; b<to; b++)
				for (int c = 0; c < (sizeof(val) / sizeof(real)); c++)
					val[c] += srcvec[b].er * srcmatrix[a*(sizeof(val) / sizeof(real))+from2+c+b*matrix_width].weight;
			for (int c = 0; (c < sizeof(val) / sizeof(real)); c++)
				dest[a*(sizeof(val) / sizeof(real))+from2+c].er += val[c];
		}
	
		for (a=a*(sizeof(val) / sizeof(real)); a<to2-from2; a++) {
			for (b=from; b<to; b++) {
				dest[a+from2].er += srcvec[b].er * srcmatrix[a+from2+b*matrix_width].weight;
			}
		}
    	
		if (gradient_cutoff>0)
		for (a=from2; a<to2; a++) {
			if (dest[a].er>gradient_cutoff) dest[a].er=gradient_cutoff;
			if (dest[a].er<-gradient_cutoff) dest[a].er=-gradient_cutoff;
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

void CRnnLM::sigmoidActivation(Neuron *neurons, int num_neurons)
{
	for (int layer_index = 0; layer_index < num_neurons; layer_index++) 
		neurons[layer_index].sigmoidActivation();
}

void CRnnLM::randomizeWeights(Synapse *synapses, int src_size, int dest_size)
{
	for (int dest_index = 0; dest_index < dest_size; dest_index++) 
		for (int src_index = 0; src_index < src_size; src_index++)
			synapses[src_index + dest_index * src_size].weight = random(-0.1, 0.1) + random(-0.1, 0.1) + random(-0.1, 0.1);
}

void CRnnLM::layer_clearActivation(Neuron neurons[], int layer_size)
{
	for (int neuron_index = 0; neuron_index < layer_size; neuron_index++) 
		neurons[neuron_index].ac = 0;
}

void CRnnLM::layer2_clearActivation(Neuron neurons[], int first_neuron, int num_neurons)
{
	for (int neuron_index = first_neuron; neuron_index < num_neurons; neuron_index++) 
		neurons[neuron_index].ac = 0;
}

void CRnnLM::layer_clearError(Neuron *neurons, int layer_size)
{
	for (int neuron_index = 0; neuron_index < layer_size; neuron_index++) 
		neurons[neuron_index].er = 0;
}

void CRnnLM::normalizeOutputClassActivation()
{
	double sum = 0.0;   //sum is used for normalization: it's better to have larger precision as many numbers are summed together here
	real max = -FLT_MAX;
	for (int layer2_index = vocab._size; layer2_index < layer2._size; layer2_index++)
		if (layer2._neurons[layer2_index].ac > max) max = layer2._neurons[layer2_index].ac; //this prevents the need to check for overflow
	for (int layer2_index = vocab._size; layer2_index < layer2._size; layer2_index++)
		sum += fasterexp(layer2._neurons[layer2_index].ac - max);

	for (int layer2_index = vocab._size; layer2_index < layer2._size; layer2_index++)
		layer2._neurons[layer2_index].ac = fasterexp(layer2._neurons[layer2_index].ac - max) / sum; 
}

void CRnnLM::layer2_normalizeActivation(int word)
{
	double sum = 0.0;   //sum is used for normalization: it's better to have larger precision as many numbers are summed together here
	int a;
	real maxAc=-FLT_MAX;
	for (int c=0; c<class_word_count[vocab._words[word].class_index]; c++) {
		a=class_words[vocab._words[word].class_index][c];
		if (layer2._neurons[a].ac>maxAc) maxAc=layer2._neurons[a].ac;
	}
	for (int c=0; c<class_word_count[vocab._words[word].class_index]; c++) {
		a=class_words[vocab._words[word].class_index][c];
		sum+=fasterexp(layer2._neurons[a].ac-maxAc);
	}
	for (int c = 0; c < class_word_count[vocab._words[word].class_index]; c++) {
		a=class_words[vocab._words[word].class_index][c];
		layer2._neurons[a].ac=fasterexp(layer2._neurons[a].ac-maxAc)/sum; //this prevents the need to check for overflow
	}
}

void CRnnLM::clearClassActivation(int word)
{
	for (int c = 0; c < class_word_count[vocab._words[word].class_index]; c++)
		layer2._neurons[class_words[vocab._words[word].class_index][c]].ac = 0;
}

void CRnnLM::layer_receiveActivation(Neuron dest[], int dest_size, Neuron src[], int src_size, int src_index, Synapse matrix[]) {
	for (int index = 0; index < dest_size; index++)
		dest[index].ac += src[src_index].ac * matrix[src_index + index * src_size].weight;
}

void CRnnLM::computeProbDist(int last_word, int word)
{
	int a, b, c;
    
	if (last_word!=-1) layer0._neurons[last_word].ac=1;

	//propagate 0->1
	layer_clearActivation(layer1._neurons, layer1._size);
	layer_clearActivation(layerc._neurons, layerc._size);
    
	// Propagate activation of prior hidden layer into current hidden layer
	matrixXvector(layer1._neurons, layer0._neurons, syn0, layer0._size, 0, layer1._size, layer0._size-layer1._size, layer0._size, 0);

	// Propagate activation of last word into new hidden layer
	if (last_word != -1)
		layer_receiveActivation(layer1._neurons, layer1._size, layer0._neurons, layer0._size, last_word, syn0);

	//activate 1      --sigmoid
    sigmoidActivation(layer1._neurons, layer1._size);
	if (layerc._size>0) {
		matrixXvector(layerc._neurons, layer1._neurons, syn1, layer1._size, 0, layerc._size, 0, layer1._size, 0);
		//activate compression      --sigmoid
		sigmoidActivation(layerc._neurons, layerc._size);
	}
        
	//1->2 class
	layer2_clearActivation(layer2._neurons, vocab._size, layer2._size);
    
	if (layerc._size>0) {
		// Propagate activation of compression layer into output layer
		matrixXvector(layer2._neurons, layerc._neurons, sync, layerc._size, vocab._size, layer2._size, 0, layerc._size, 0);
	}
	else
	{
		// Propagate activation of layer 1 into output layer
		matrixXvector(layer2._neurons, layer1._neurons, syn1, layer1._size, vocab._size, layer2._size, 0, layer1._size, 0);
	}

	//apply direct connections to classes
	if (direct_size>0) {
		unsigned long long hash[MAX_NGRAM_ORDER];	//this will hold pointers to syn_d that contains hash parameters
	
		for (a=0; a<direct_order; a++) hash[a]=0;
	
		for (a=0; a<direct_order; a++) {
			b=0;
			if (a>0) if (history[a-1]==-1) break;	//if OOV was in history, do not use this N-gram feature and higher orders
			hash[a]=PRIMES[0]*PRIMES[1];
	    	    
			for (b=1; b<=a; b++) hash[a]+=PRIMES[(a*PRIMES[b]+b)%PRIMES_SIZE]*(unsigned long long)(history[b-1]+1);	//update hash value based on words from the history
			hash[a]=hash[a]%(direct_size/2);		//make sure that starting hash index is in the first half of syn_d (second part is reserved for history->words features)
		}
	
		for (b=0; b<direct_order; b++) 
			for (a=vocab._size; a<layer2._size; a++) {
				if (hash[b]) {
					layer2._neurons[a].ac+=syn_d[hash[b]];		//apply current parameter and move to the next one
					hash[b]++;
				} else break;
			}
	}

	//activation 2   --softmax on classes
	// 20130425 - this is now a 'safe' softmax

	normalizeOutputClassActivation();
 
	if (gen>0) return;	//if we generate words, we don't know what current word is -> only classes are estimated and word is selected in testGen()

    
	//1->2 word
    
	if (word!=-1) {
		clearClassActivation(word);
		if (layerc._size>0) {
			// Propagate activation of compression layer into output layer
			matrixXvector(layer2._neurons, layerc._neurons, sync, layerc._size, class_words[vocab._words[word].class_index][0], class_words[vocab._words[word].class_index][0]+class_word_count[vocab._words[word].class_index], 0, layerc._size, 0);
		}
		else
		{
			// Propagate activation of layer1 into output layer
			matrixXvector(layer2._neurons, layer1._neurons, syn1, layer1._size, class_words[vocab._words[word].class_index][0], class_words[vocab._words[word].class_index][0]+class_word_count[vocab._words[word].class_index], 0, layer1._size, 0);
		}
	}
    
	//apply direct connections to words
	if (word!=-1) if (direct_size>0) {
		unsigned long long hash[MAX_NGRAM_ORDER];
	    
		for (a=0; a<direct_order; a++) hash[a]=0;
	
		for (a=0; a<direct_order; a++) {
			b=0;
			if (a>0) if (history[a-1]==-1) break;
			hash[a]=PRIMES[0]*PRIMES[1]*(unsigned long long)(vocab._words[word].class_index+1);
				
			for (b=1; b<=a; b++) hash[a]+=PRIMES[(a*PRIMES[b]+b)%PRIMES_SIZE]*(unsigned long long)(history[b-1]+1);
			hash[a]=(hash[a]%(direct_size/2))+(direct_size)/2;
		}
	
		for (c=0; c<class_word_count[vocab._words[word].class_index]; c++) {
			a=class_words[vocab._words[word].class_index][c];
	    
			for (b=0; b<direct_order; b++) if (hash[b]) {
				layer2._neurons[a].ac+=syn_d[hash[b]];
				hash[b]++;
				hash[b]=hash[b]%direct_size;
			} else break;
		}
	}

	if (word!=-1)
		layer2_normalizeActivation(word);
}

void CRnnLM::computeErrorVectors(int word)
{
	for (int c = 0; c < class_word_count[vocab._words[word].class_index]; c++) {
		layer2._neurons[class_words[vocab._words[word].class_index][c]].er = (0 - layer2._neurons[class_words[vocab._words[word].class_index][c]].ac);
	}
	layer2._neurons[word].er=(1-layer2._neurons[word].ac);	//word part

	for (int a = vocab._size; a < layer2._size; a++) {
		layer2._neurons[a].er = (0 - layer2._neurons[a].ac);
	}
	layer2._neurons[vocab._size + vocab._words[word].class_index].er = (1 - layer2._neurons[vocab._size + vocab._words[word].class_index].ac);	//class part
}

void CRnnLM::adjustWeights(int counter, int b, int t, real beta2)
{
	if ((counter % 10) == 0)	//regularization is done every 10 steps
		for (int a = 0; a < layer1._size; a++) syn1[a + t].weight += alpha * layer2._neurons[b].er * layer1._neurons[a].ac - syn1[a + t].weight * beta2;
	else
		for (int a = 0; a < layer1._size; a++) syn1[a + t].weight += alpha * layer2._neurons[b].er * layer1._neurons[a].ac;
}

void CRnnLM::learn(int last_word, int word)
{
	int a, b, c, t, step;
	real beta2, beta3;

	beta2 = beta * alpha;
	beta3 = beta2 * 1;	//beta3 can be possibly larger than beta2, as that is useful on small datasets (if the final model is to be interpolated wich backoff model) - todo in the future

	if (word == -1) return;

	computeErrorVectors(word);

	//flush error
	layer_clearError(layer1._neurons, layer1._size);
	layer_clearError(layerc._neurons, layerc._size);
    
	if (direct_size > 0) {	//learn direct connections between words
		if (word != -1) {
			unsigned long long hash[MAX_NGRAM_ORDER];
	    
			for (a=0; a<direct_order; a++) hash[a]=0;
	
			for (a=0; a<direct_order; a++) {
				b=0;
				if (a>0) if (history[a-1]==-1) break;
				hash[a]=PRIMES[0]*PRIMES[1]*(unsigned long long)(vocab._words[word].class_index+1);
				
				for (b=1; b<=a; b++) hash[a]+=PRIMES[(a*PRIMES[b]+b)%PRIMES_SIZE]*(unsigned long long)(history[b-1]+1);
				hash[a]=(hash[a]%(direct_size/2))+(direct_size)/2;
			}
	
			for (c=0; c<class_word_count[vocab._words[word].class_index]; c++) {
				a=class_words[vocab._words[word].class_index][c];
	    
				for (b=0; b<direct_order; b++) if (hash[b]) {
					syn_d[hash[b]]+=alpha*layer2._neurons[a].er - syn_d[hash[b]]*beta3;
					hash[b]++;
					hash[b]=hash[b]%direct_size;
				} else break;
			}
		}
		//learn direct connections to classes
		//learn direct connections between words and classes
		unsigned long long hash[MAX_NGRAM_ORDER];
	
		for (a=0; a<direct_order; a++) hash[a]=0;
	
		for (a=0; a<direct_order; a++) {
			b=0;
			if (a>0) if (history[a-1]==-1) break;
			hash[a]=PRIMES[0]*PRIMES[1];
	    	    
			for (b=1; b<=a; b++) hash[a]+=PRIMES[(a*PRIMES[b]+b)%PRIMES_SIZE]*(unsigned long long)(history[b-1]+1);
			hash[a]=hash[a]%(direct_size/2);
		}
	
		for (a=vocab._size; a<layer2._size; a++) {
			for (b=0; b<direct_order; b++) if (hash[b]) {
				syn_d[hash[b]]+=alpha*layer2._neurons[a].er - syn_d[hash[b]]*beta3;
				hash[b]++;
			} else break;
		}
	}
    
	if (layerc._size>0) {
		matrixXvector(layerc._neurons, layer2._neurons, sync, layerc._size, class_words[vocab._words[word].class_index][0], class_words[vocab._words[word].class_index][0]+class_word_count[vocab._words[word].class_index], 0, layerc._size, 1);
	
		t=class_words[vocab._words[word].class_index][0]*layerc._size;
		for (c=0; c<class_word_count[vocab._words[word].class_index]; c++) {
			b=class_words[vocab._words[word].class_index][c];
			adjustWeights(counter, b, t, beta2);
			t+=layerc._size;
		}

		matrixXvector(layerc._neurons, layer2._neurons, sync, layerc._size, vocab._size, layer2._size, 0, layerc._size, 1);		//propagates errors 2->c for classes
	
		t=vocab._size*layerc._size;
		for (b=vocab._size; b<layer2._size; b++) {
			adjustWeights(counter, b, t, beta2);
			t += layerc._size;
		}
	
		for (a=0; a<layerc._size; a++) layerc._neurons[a].er=layerc._neurons[a].er*layerc._neurons[a].ac*(1-layerc._neurons[a].ac);    //error derivation at compression layer
	
		matrixXvector(layer1._neurons, layerc._neurons, syn1, layer1._size, 0, layerc._size, 0, layer1._size, 1);		//propagates errors c->1
	
		for (b=0; b<layerc._size; b++) {
			for (a=0; a<layer1._size; a++) syn1[a+b*layer1._size].weight+=alpha*layerc._neurons[b].er*layer1._neurons[a].ac;	//weight 1->c update
		}
	}
	else
	{
		// propagate error from output layer to layer 1 for vocabulary
		matrixXvector(layer1._neurons, layer2._neurons, syn1, layer1._size, class_words[vocab._words[word].class_index][0], class_words[vocab._words[word].class_index][0]+class_word_count[vocab._words[word].class_index], 0, layer1._size, 1);
    	
		t = class_words[vocab._words[word].class_index][0] * layer1._size;
		for (c = 0; c < class_word_count[vocab._words[word].class_index]; c++) {
			b = class_words[vocab._words[word].class_index][c];
			adjustWeights(counter, b, t, beta2);
			t += layer1._size;
		}

		// propagate error from output layer to layer 1 for classes
		matrixXvector(layer1._neurons, layer2._neurons, syn1, layer1._size, vocab._size, layer2._size, 0, layer1._size, 1);		//propagates errors 2->1 for classes
	
		t = vocab._size * layer1._size;
		for (b = vocab._size; b < layer2._size; b++) {
			adjustWeights(counter, b, t, beta2);
			t += layer1._size;
		}
	}

	if (bptt <= 1) {		//bptt==1 -> normal BP
		for (a = 0; a < layer1._size; a++) 
			layer1._neurons[a].er = layer1._neurons[a].er * layer1._neurons[a].ac * (1 - layer1._neurons[a].ac);    //error derivation at layer 1

		//weight update 1->0
		a=last_word;
		if (a!=-1) {
			if ((counter%10)==0)
				for (b=0; b<layer1._size; b++) syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er*layer0._neurons[a].ac - syn0[a+b*layer0._size].weight*beta2;
			else
				for (b=0; b<layer1._size; b++) syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er*layer0._neurons[a].ac;
		}

		if ((counter%10)==0) {
			for (b=0; b<layer1._size; b++) for (a=layer0._size-layer1._size; a<layer0._size; a++) syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er*layer0._neurons[a].ac - syn0[a+b*layer0._size].weight*beta2;
		}
		else {
			for (b=0; b<layer1._size; b++) for (a=layer0._size-layer1._size; a<layer0._size; a++) syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er*layer0._neurons[a].ac;
		}
	}
	else		//BPTT
	{
		for (b=0; b<layer1._size; b++) bptt_hidden[b].copy(layer1._neurons[b]);
	
		if (((counter%bptt_block)==0) || (independent && (word==0))) {
			for (step=0; step<bptt+bptt_block-2; step++) {
				for (a=0; a<layer1._size; a++) layer1._neurons[a].er=layer1._neurons[a].er*layer1._neurons[a].ac*(1-layer1._neurons[a].ac);    //error derivation at layer 1

				//weight update 1->0
				a=bptt_history[step];
				if (a!=-1)
				for (b=0; b<layer1._size; b++) {
					bptt_syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er;//*layer0._neurons[a].ac; --should be always set to 1
				}
	    
				for (a=layer0._size-layer1._size; a<layer0._size; a++) layer0._neurons[a].er=0;
		
				matrixXvector(layer0._neurons, layer1._neurons, syn0, layer0._size, 0, layer1._size, layer0._size-layer1._size, layer0._size, 1);		//propagates errors 1->0
				for (b=0; b<layer1._size; b++) for (a=layer0._size-layer1._size; a<layer0._size; a++) {
					//layer0._neurons[a].er += layer1._neurons[b].er * syn0[a+b*layer0._size].weight;
					bptt_syn0[a+b*layer0._size].weight+=alpha*layer1._neurons[b].er*layer0._neurons[a].ac;
				}
	    
				for (a=0; a<layer1._size; a++) {		//propagate error from time T-n to T-n-1
					layer1._neurons[a].er=layer0._neurons[a+layer0._size-layer1._size].er + bptt_hidden[(step+1)*layer1._size+a].er;
				}
	    
				if (step<bptt+bptt_block-3)
				for (a=0; a<layer1._size; a++) {
					layer1._neurons[a].ac=bptt_hidden[(step+1)*layer1._size+a].ac;
					layer0._neurons[a+layer0._size-layer1._size].ac=bptt_hidden[(step+2)*layer1._size+a].ac;
				}
			}
	    
			for (a=0; a<(bptt+bptt_block)*layer1._size; a++) {
				bptt_hidden[a].er=0;
			}
	
	
			for (b=0; b<layer1._size; b++) layer1._neurons[b].ac=bptt_hidden[b].ac;		//restore hidden layer after bptt
		
	
			//
			for (b=0; b<layer1._size; b++) {		//copy temporary syn0
				if ((counter%10)==0) {
					for (a=layer0._size-layer1._size; a<layer0._size; a++) {
						syn0[a+b*layer0._size].weight+=bptt_syn0[a+b*layer0._size].weight - syn0[a+b*layer0._size].weight*beta2;
						bptt_syn0[a+b*layer0._size].weight=0;
					}
				}
				else {
					for (a=layer0._size-layer1._size; a<layer0._size; a++) {
						syn0[a+b*layer0._size].weight+=bptt_syn0[a+b*layer0._size].weight;
						bptt_syn0[a+b*layer0._size].weight=0;
					}
				}
	    
				if ((counter%10)==0) {
					for (step=0; step<bptt+bptt_block-2; step++) if (bptt_history[step]!=-1) {
						syn0[bptt_history[step]+b*layer0._size].weight+=bptt_syn0[bptt_history[step]+b*layer0._size].weight - syn0[bptt_history[step]+b*layer0._size].weight*beta2;
						bptt_syn0[bptt_history[step]+b*layer0._size].weight=0;
					}
				}
				else {
					for (step=0; step<bptt+bptt_block-2; step++) if (bptt_history[step]!=-1) {
						syn0[bptt_history[step]+b*layer0._size].weight+=bptt_syn0[bptt_history[step]+b*layer0._size].weight;
						bptt_syn0[bptt_history[step]+b*layer0._size].weight=0;
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
	int a, b, word, last_word, wordcn;
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

	if (class_size>vocab._size) {
		printf("WARNING: number of classes exceeds vocabulary size!\n");
	}
    
	counter=train_cur_pos;
	//saveNet();
	while (iter < maxIter) {
		printf("Iter: %3d\tAlpha: %f\t   ", iter, alpha);
		fflush(stdout);
        
		if (bptt>0) for (a=0; a<bptt+bptt_block; a++) bptt_history[a]=0;
		for (a=0; a<MAX_NGRAM_ORDER; a++) history[a]=0;

		//TRAINING PHASE
		netFlush();

		fi=fopen(train_file, "rb");
		last_word=0;
        
		if (counter>0) for (a=0; a<counter; a++) word=readWordIndex(fi);	//this will skip words that were already learned if the training was interrupted
        
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
        
			word=readWordIndex(fi);     //read next word
			computeProbDist(last_word, word);      //compute probability distribution
			if (feof(fi)) break;        //end of file: test on validation data, iterate till convergence

			if (word!=-1) logp += log10(layer2._neurons[vocab._size + vocab._words[word].class_index].ac * layer2._neurons[word].ac);
    	    
			if ((logp != logp) || (isinf(logp))) {
				printf("\nNumerical error %d %f %f\n", word, layer2._neurons[word].ac, layer2._neurons[vocab._words[word].class_index + vocab._size].ac);
				exit(1);
			}

			if (bptt>0) {		//shift memory needed for bptt to next time step
				for (a = bptt + bptt_block - 1; a > 0; a--) bptt_history[a] = bptt_history[a - 1];
				bptt_history[0] = last_word;
		
				for (a = bptt + bptt_block - 1; a > 0; a--) 
					for (b = 0; b < layer1._size; b++) {
						bptt_hidden[a * layer1._size + b].copy(bptt_hidden[(a - 1) * layer1._size+b]);
					}
			}

			learn(last_word, word);
            
			copyHiddenLayerToInput();

			if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation

			last_word=word;
            
			for (a=MAX_NGRAM_ORDER-1; a>0; a--) history[a]=history[a-1];
			history[0]=last_word;

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
            
			for (a=MAX_NGRAM_ORDER-1; a>0; a--) history[a]=history[a-1];
			history[0]=last_word;

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
	int a, b, word, last_word, wordcn;
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
    
	if (bptt>0) for (a=0; a<bptt+bptt_block; a++) bptt_history[a]=0;
	for (a=0; a<MAX_NGRAM_ORDER; a++) history[a]=0;
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

		if (dynamic>0) {
			if (bptt>0) {
				for (a=bptt+bptt_block-1; a>0; a--) bptt_history[a]=bptt_history[a-1];
				bptt_history[0]=last_word;
                                    
				for (a=bptt+bptt_block-1; a>0; a--) for (b=0; b<layer1._size; b++) {
					bptt_hidden[a*layer1._size+b].copy(bptt_hidden[(a-1)*layer1._size+b]);
				}
			}
			//
			alpha=dynamic;
			learn(last_word, word);    //dynamic update
		}
		copyHiddenLayerToInput();
        
		if (last_word!=-1) layer0._neurons[last_word].ac=0;  //delete previous activation

		last_word=word;
        
		for (a=MAX_NGRAM_ORDER-1; a>0; a--) history[a]=history[a-1];
		history[0]=last_word;

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
	int a, word, last_word, wordcn;
	FILE *fi, *flog, *lmprob=NULL;
	float prob_other; //has to be float so that %f works in fscanf
	real log_other, log_combine, senp;
	//int nbest=-1;
	int nbest_cn=0;
	char ut1[MAX_STRING], ut2[MAX_STRING];

	restoreNet();
	computeProbDist(0, 0);
	copyHiddenLayerToInput();
	layer_copy_layer(neu1b, layer1._size, layer1._neurons);
	layer_copy_layer(neu1b2, layer1._size, layer1._neurons);
    
	if (use_lmprob) {
		lmprob=fopen(lmprob_file, "rb");
	} else lambda=1;		//!!! for simpler implementation later

	//TEST PHASE
	//netFlush();
    
	for (a=0; a<MAX_NGRAM_ORDER; a++) history[a]=0;

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
	    
			if (nbest_cn==1) layer_copy_layer(neu1b2, layer1._size, layer1._neurons);		//save context after processing first sentence in nbest
	    
			if (strcmp(ut1, ut2)) {
				strcpy(ut1, ut2);
				nbest_cn=0;
				layer_copy_layer(layer1._neurons, layer1._size, neu1b2);
				layer_copy_layer(neu1b, layer1._size, layer1._neurons);
			} else 
				layer_copy_layer(layer1._neurons, layer1._size, neu1b);
	    
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
        
		for (a=MAX_NGRAM_ORDER-1; a>0; a--) history[a]=history[a-1];
		history[0]=last_word;

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
	int i, word, cla, last_word, wordcn, c, b, a=0;
	real f, g, sum;
    
	restoreNet();
    
	word=0;
	last_word=0;					//last word = end of sentence
	wordcn=0;
	copyHiddenLayerToInput();
	while (wordcn<gen) {
		computeProbDist(last_word, 0);		//compute probability distribution
        
		f=random(0, 1);
		g=0;
		i=vocab._size;
		while ((g<f) && (i<layer2._size)) {
			g+=layer2._neurons[i].ac;
			i++;
		}
		cla=i-1-vocab._size;
        
		if (cla>class_size-1) cla=class_size-1;
		if (cla<0) cla=0;
        
		//
		// !!!!!!!!  THIS WILL WORK ONLY IF CLASSES ARE CONTINUALLY DEFINED IN VOCAB !!! (like class 10 = words 11 12 13; not 11 12 16)  !!!!!!!!
		// forward pass 1->2 for words
		for (c=0; c<class_word_count[cla]; c++) layer2._neurons[class_words[cla][c]].ac=0;
		matrixXvector(layer2._neurons, layer1._neurons, syn1, layer1._size, class_words[cla][0], class_words[cla][0]+class_word_count[cla], 0, layer1._size, 0);
        
		//apply direct connections to words
		if (word!=-1) if (direct_size>0) {
			unsigned long long hash[MAX_NGRAM_ORDER];

			for (a=0; a<direct_order; a++) hash[a]=0;

			for (a=0; a<direct_order; a++) {
				b=0;
				if (a>0) if (history[a-1]==-1) break;
				hash[a]=PRIMES[0]*PRIMES[1]*(unsigned long long)(cla+1);

				for (b=1; b<=a; b++) hash[a]+=PRIMES[(a*PRIMES[b]+b)%PRIMES_SIZE]*(unsigned long long)(history[b-1]+1);
				hash[a]=(hash[a]%(direct_size/2))+(direct_size)/2;
			}

			for (c=0; c<class_word_count[cla]; c++) {
				a=class_words[cla][c];

				for (b=0; b<direct_order; b++) if (hash[b]) {
					layer2._neurons[a].ac+=syn_d[hash[b]];
					hash[b]++;
					hash[b]=hash[b]%direct_size;
				} else break;
			}
		}
        
		//activation 2   --softmax on words
		// 130425 - this is now a 'safe' softmax

		sum=0;
		real maxAc=-FLT_MAX;
		for (c=0; c<class_word_count[cla]; c++) {
			a=class_words[cla][c];
			if (layer2._neurons[a].ac>maxAc) maxAc=layer2._neurons[a].ac;
		}
		for (c=0; c<class_word_count[cla]; c++) {
			a=class_words[cla][c];
			sum+=fasterexp(layer2._neurons[a].ac-maxAc);
		}
		for (c=0; c<class_word_count[cla]; c++) {
			a=class_words[cla][c];
			layer2._neurons[a].ac=fasterexp(layer2._neurons[a].ac-maxAc)/sum; //this prevents the need to check for overflow
		}
		//
	
		f=random(0, 1);
		g=0;
		/*i=0;
		while ((g<f) && (i<vocab._size)) {
		g+=layer2._neurons[i].ac;
		i++;
		}*/
		for (c=0; c<class_word_count[cla]; c++) {
			a=class_words[cla][c];
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
        
		for (a=MAX_NGRAM_ORDER-1; a>0; a--) history[a]=history[a-1];
		history[0]=last_word;

		if (independent && (word==0)) netReset();
        
		wordcn++;
	}
}
