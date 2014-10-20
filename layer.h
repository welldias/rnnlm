#ifndef _LAYER_H_

#define _LAYER_H_

class Layer {
public:
	Neuron *_neurons;
	int _size;

	Layer() {
		_neurons = NULL;
		_size = 0;
	}
	~Layer() {
		if (_neurons != NULL) free(_neurons);
	}
	void copy(const Layer &);
	void clearActivation();
	void clearError();
	void clear();
	void print(FILE *);
	void write(FILE *);
	void scan(FILE *);
	void read(FILE *);
	void setActivation(real);
	void receiveActivation(Layer &, int, Synapse []);
};

#endif