#include "rag.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <ctype.h>

#define HASH_SEED 5381

static uint64_t hash_bytes(const uint8_t *data, size_t len) {
    uint64_t hash = HASH_SEED;
    for (size_t i = 0; i < len; i++) {
        hash = hash * 33 + data[i];
    }
    return hash;
}

void rag_compute_embedding(const char *text, double *embedding) {
    if (!text || !embedding) return;
    
    size_t text_len = strlen(text);
    if (text_len == 0) {
        for (size_t i = 0; i < RAG_EMBEDDING_DIM; i++) {
            embedding[i] = 0.0;
        }
        return;
    }
    
    for (size_t i = 0; i < RAG_EMBEDDING_DIM; i++) {
        uint8_t buf[256];
        size_t offset = 0;
        
        uint64_t h1 = hash_bytes((const uint8_t*)text, text_len);
        uint64_t h2 = hash_bytes((const uint8_t*)&i, sizeof(i));
        uint64_t h3 = hash_bytes((const uint8_t*)"embed", 5);
        
        memcpy(buf + offset, &h1, sizeof(h1));
        offset += sizeof(h1);
        memcpy(buf + offset, &h2, sizeof(h2));
        offset += sizeof(h2);
        memcpy(buf + offset, &h3, sizeof(h3));
        
        uint64_t h = hash_bytes(buf, offset);
        double normalized = (double)(h % 10000) / 10000.0;
        embedding[i] = (normalized * 2.0) - 1.0;
    }
    
    double norm = 0.0;
    for (size_t i = 0; i < RAG_EMBEDDING_DIM; i++) {
        norm += embedding[i] * embedding[i];
    }
    norm = sqrt(norm);
    if (norm > 0.0) {
        for (size_t i = 0; i < RAG_EMBEDDING_DIM; i++) {
            embedding[i] /= norm;
        }
    }
}

double rag_cosine_similarity(const double *a, const double *b, size_t dim) {
    if (!a || !b) return 0.0;
    
    double dot = 0.0;
    double norm_a = 0.0;
    double norm_b = 0.0;
    
    for (size_t i = 0; i < dim; i++) {
        dot += a[i] * b[i];
        norm_a += a[i] * a[i];
        norm_b += b[i] * b[i];
    }
    
    double denom = sqrt(norm_a) * sqrt(norm_b);
    if (denom == 0.0) return 0.0;
    
    return dot / denom;
}

void rag_chunk_text(const char *text, size_t chunk_size, char (*chunks)[RAG_CHUNK_SIZE], size_t *chunk_count) {
    if (!text || !chunks || !chunk_count) return;
    
    *chunk_count = 0;
    size_t text_len = strlen(text);
    if (text_len == 0) return;
    
    size_t pos = 0;
    while (pos < text_len && *chunk_count < 10) {
        size_t remaining = text_len - pos;
        size_t this_chunk = remaining < chunk_size ? remaining : chunk_size;
        
        memcpy(chunks[*chunk_count], text + pos, this_chunk);
        chunks[*chunk_count][this_chunk] = '\0';
        
        (*chunk_count)++;
        pos += this_chunk;
        
        if (pos < text_len && *chunk_count < 10) {
            size_t overlap = this_chunk > 50 ? 50 : 0;
            pos -= overlap;
        }
    }
}

int rag_index_init(rag_index_t *idx) {
    if (!idx) return -1;
    memset(idx, 0, sizeof(rag_index_t));
    idx->count = 0;
    idx->index_path[0] = '\0';
    return 0;
}

int rag_index_init_file(rag_index_t *idx, const char *path) {
    if (!idx || !path) return -1;
    rag_index_init(idx);
    snprintf(idx->index_path, sizeof(idx->index_path), "%s", path);
    return rag_index_load(idx, path);
}

int rag_index_add(rag_index_t *idx, const char *document, const char *source) {
    if (!idx || !document) return -1;
    if (idx->count >= RAG_MAX_CHUNKS) return -1;
    
    rag_chunk_t *chunk = &idx->chunks[idx->count];
    snprintf(chunk->document, sizeof(chunk->document), "%s", document);
    if (source) {
        snprintf(chunk->source, sizeof(chunk->source), "%s", source);
    }
    chunk->timestamp = (uint64_t)time(NULL);
    chunk->has_embedding = false;
    
    rag_compute_embedding(document, chunk->embedding);
    chunk->has_embedding = true;
    chunk->embedding_dim = RAG_EMBEDDING_DIM;
    
    idx->count++;
    return 0;
}

int rag_index_add_with_embedding(rag_index_t *idx, const char *document, const char *source, const double *embedding) {
    if (!idx || !document) return -1;
    if (idx->count >= RAG_MAX_CHUNKS) return -1;
    
    rag_chunk_t *chunk = &idx->chunks[idx->count];
    snprintf(chunk->document, sizeof(chunk->document), "%s", document);
    if (source) {
        snprintf(chunk->source, sizeof(chunk->source), "%s", source);
    }
    chunk->timestamp = (uint64_t)time(NULL);
    
    if (embedding) {
        memcpy(chunk->embedding, embedding, RAG_EMBEDDING_DIM * sizeof(double));
        chunk->has_embedding = true;
        chunk->embedding_dim = RAG_EMBEDDING_DIM;
    } else {
        rag_compute_embedding(document, chunk->embedding);
        chunk->has_embedding = true;
        chunk->embedding_dim = RAG_EMBEDDING_DIM;
    }
    
    idx->count++;
    return 0;
}

int rag_index_search(rag_index_t *idx, const double *query_embedding, size_t top_k, rag_result_t *result) {
    if (!idx || !query_embedding || !result) return -1;
    if (idx->count == 0) {
        result->chunk_count = 0;
        return 0;
    }
    
    typedef struct {
        size_t index;
        double score;
    } scored_chunk_t;
    
    scored_chunk_t scores[RAG_MAX_CHUNKS];
    size_t score_count = 0;
    
    for (size_t i = 0; i < idx->count; i++) {
        if (!idx->chunks[i].has_embedding) continue;
        
        double sim = rag_cosine_similarity(query_embedding, idx->chunks[i].embedding, RAG_EMBEDDING_DIM);
        
        if (score_count < RAG_MAX_CHUNKS) {
            scores[score_count].index = i;
            scores[score_count].score = sim;
            score_count++;
        }
    }
    
    for (size_t i = 0; i < score_count; i++) {
        for (size_t j = i + 1; j < score_count; j++) {
            if (scores[j].score > scores[i].score) {
                scored_chunk_t tmp = scores[i];
                scores[i] = scores[j];
                scores[j] = tmp;
            }
        }
    }
    
    size_t count = top_k < score_count ? top_k : score_count;
    result->chunk_count = count;
    
    for (size_t i = 0; i < count; i++) {
        const char *doc = idx->chunks[scores[i].index].document;
        size_t len = strlen(doc);
        if (len >= RAG_CHUNK_SIZE) len = RAG_CHUNK_SIZE - 1;
        memcpy(result->chunks[i], doc, len);
        result->chunks[i][len] = '\0';
        result->scores[i] = scores[i].score;
    }
    
    return 0;
}

int rag_index_query(rag_index_t *idx, const char *query, size_t top_k, rag_result_t *result) {
    if (!idx || !query || !result) return -1;
    
    double query_embedding[RAG_EMBEDDING_DIM];
    rag_compute_embedding(query, query_embedding);
    
    return rag_index_search(idx, query_embedding, top_k, result);
}

int rag_index_save(rag_index_t *idx, const char *path) {
    if (!idx || !path) return -1;
    
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    
    fwrite(&idx->count, sizeof(size_t), 1, f);
    
    for (size_t i = 0; i < idx->count; i++) {
        rag_chunk_t *chunk = &idx->chunks[i];
        
        size_t doc_len = strlen(chunk->document) + 1;
        fwrite(&doc_len, sizeof(size_t), 1, f);
        fwrite(chunk->document, 1, doc_len, f);
        
        size_t src_len = strlen(chunk->source) + 1;
        fwrite(&src_len, sizeof(size_t), 1, f);
        fwrite(chunk->source, 1, src_len, f);
        
        fwrite(&chunk->timestamp, sizeof(uint64_t), 1, f);
        fwrite(&chunk->has_embedding, sizeof(bool), 1, f);
        fwrite(chunk->embedding, sizeof(double), RAG_EMBEDDING_DIM, f);
    }
    
    fclose(f);
    return 0;
}

int rag_index_load(rag_index_t *idx, const char *path) {
    if (!idx || !path) return -1;
    
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    
    size_t count;
    if (fread(&count, sizeof(size_t), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    
    if (count > RAG_MAX_CHUNKS) {
        fclose(f);
        return -1;
    }
    
    for (size_t i = 0; i < count; i++) {
        rag_chunk_t *chunk = &idx->chunks[i];
        
        size_t doc_len;
        fread(&doc_len, sizeof(size_t), 1, f);
        fread(chunk->document, 1, doc_len, f);
        
        size_t src_len;
        fread(&src_len, sizeof(size_t), 1, f);
        fread(chunk->source, 1, src_len, f);
        
        fread(&chunk->timestamp, sizeof(uint64_t), 1, f);
        fread(&chunk->has_embedding, sizeof(bool), 1, f);
        fread(chunk->embedding, sizeof(double), RAG_EMBEDDING_DIM, f);
        
        chunk->embedding_dim = RAG_EMBEDDING_DIM;
    }
    
    idx->count = count;
    fclose(f);
    return 0;
}

void rag_index_free(rag_index_t *idx) {
    if (idx) {
        memset(idx, 0, sizeof(rag_index_t));
    }
}
