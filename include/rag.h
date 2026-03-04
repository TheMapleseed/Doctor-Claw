#ifndef DOCTORCLAW_RAG_H
#define DOCTORCLAW_RAG_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RAG_MAX_CHUNKS 256
#define RAG_EMBEDDING_DIM 384
#define RAG_CHUNK_SIZE 512

typedef struct {
    char document[16384];
    char source[256];
    uint64_t timestamp;
    double embedding[RAG_EMBEDDING_DIM];
    size_t embedding_dim;
    bool has_embedding;
} rag_chunk_t;

typedef struct {
    rag_chunk_t chunks[RAG_MAX_CHUNKS];
    size_t count;
    char index_path[512];
} rag_index_t;

typedef struct {
    char query[4096];
    double embedding[RAG_EMBEDDING_DIM];
    size_t top_k;
    double min_similarity;
} rag_query_t;

typedef struct {
    char chunks[10][RAG_CHUNK_SIZE];
    size_t chunk_count;
    double scores[10];
} rag_result_t;

int rag_index_init(rag_index_t *idx);
int rag_index_init_file(rag_index_t *idx, const char *path);
int rag_index_add(rag_index_t *idx, const char *document, const char *source);
int rag_index_add_with_embedding(rag_index_t *idx, const char *document, const char *source, const double *embedding);
int rag_index_query(rag_index_t *idx, const char *query, size_t top_k, rag_result_t *result);
int rag_index_save(rag_index_t *idx, const char *path);
int rag_index_load(rag_index_t *idx, const char *path);
int rag_index_search(rag_index_t *idx, const double *query_embedding, size_t top_k, rag_result_t *result);
void rag_compute_embedding(const char *text, double *embedding);
double rag_cosine_similarity(const double *a, const double *b, size_t dim);
void rag_chunk_text(const char *text, size_t chunk_size, char (*chunks)[RAG_CHUNK_SIZE], size_t *chunk_count);
void rag_index_free(rag_index_t *idx);

#endif
