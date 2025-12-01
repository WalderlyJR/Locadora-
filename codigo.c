#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define M 5
#define P 3
#define MAX_KEYS (M - 1)
#define MIN_KEYS ((M / 2) - 1)

#define TAMANHO_PLACA 8
#define TAMANHO_MODELO 20
#define TAMANHO_MARCA 20
#define TAMANHO_CATEGORIA 15
#define TAMANHO_STATUS 16

typedef struct {
    char placa[TAMANHO_PLACA];
    char modelo[TAMANHO_MODELO];
    char marca[TAMANHO_MARCA];
    int ano;
    char categoria[TAMANHO_CATEGORIA];
    int quilometragem;
    char status[TAMANHO_STATUS];
} Veiculo;

typedef struct BTreeNode {
    char keys[MAX_KEYS][TAMANHO_PLACA];
    int rrns[MAX_KEYS];
    int children[M];
    int num_keys;
    int parent_rrn;
    bool is_leaf;
    bool modified;
} BTreeNode;

typedef struct PageQueueNode {
    BTreeNode *page;
    int rrn;
    struct PageQueueNode *next;
} PageQueueNode;

typedef struct PageQueue {
    PageQueueNode *front;
    PageQueueNode *rear;
    int size;
} PageQueue;

typedef struct BTree {
    FILE *index_file;
    FILE *data_file;
    FILE *text_file;
    int root_rrn;
    int next_rrn;
    char index_filename[256];
    char data_filename[256];
    char text_filename[256];
    PageQueue *page_queue;
} BTree;

// Funcoes auxiliares
void limpar_buffer() {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

void normalizar_placa(char *placa) {
    int len = strlen(placa);
    while (len > 0 && (placa[len-1] == ' ' || placa[len-1] == '\n' || placa[len-1] == '\r')) {
        placa[len-1] = '\0';
        len--;
    }
    
    int start = 0;
    while (placa[start] == ' ') {
        start++;
    }
    if (start > 0) {
        memmove(placa, placa + start, strlen(placa + start) + 1);
    }
    
    for (int i = 0; placa[i] != '\0'; i++) {
        if (placa[i] >= 'a' && placa[i] <= 'z') {
            placa[i] = placa[i] - 'a' + 'A';
        }
    }
}

void ler_string(char *destino, int tamanho, const char *prompt) {
    printf("%s", prompt);
    fflush(stdout);
    
    if (fgets(destino, tamanho, stdin) != NULL) {
        destino[strcspn(destino, "\n")] = 0;
    }
}

int ler_inteiro(const char *prompt) {
    char buffer[100];
    printf("%s", prompt);
    fflush(stdout);
    
    if (fgets(buffer, sizeof(buffer), stdin) != NULL) {
        return atoi(buffer);
    }
    return 0;
}

// Fila de paginas
PageQueue* queue_create() {
    PageQueue *queue = (PageQueue*)malloc(sizeof(PageQueue));
    queue->front = NULL;
    queue->rear = NULL;
    queue->size = 0;
    return queue;
}

void queue_destroy(PageQueue *queue) {
    PageQueueNode *current = queue->front;
    while (current != NULL) {
        PageQueueNode *temp = current;
        current = current->next;
        free(temp->page);
        free(temp);
    }
    free(queue);
}

bool queue_is_full(PageQueue *queue) {
    return queue->size >= P;
}

BTreeNode* queue_find(PageQueue *queue, int rrn) {
    PageQueueNode *current = queue->front;
    while (current != NULL) {
        if (current->rrn == rrn) {
            return current->page;
        }
        current = current->next;
    }
    return NULL;
}

void queue_move_to_end(PageQueue *queue, int rrn) {
    if (queue->size <= 1) return;
    
    PageQueueNode *current = queue->front;
    PageQueueNode *prev = NULL;
    
    while (current != NULL && current->rrn != rrn) {
        prev = current;
        current = current->next;
    }
    
    if (current == NULL || current == queue->rear) return;
    
    if (prev == NULL) {
        queue->front = current->next;
    } else {
        prev->next = current->next;
    }
    
    queue->rear->next = current;
    queue->rear = current;
    current->next = NULL;
}

void queue_remove_lru(PageQueue *queue, BTree *tree);

void queue_add(PageQueue *queue, BTreeNode *page, int rrn) {
    PageQueueNode *new_node = (PageQueueNode*)malloc(sizeof(PageQueueNode));
    new_node->page = page;
    new_node->rrn = rrn;
    new_node->next = NULL;
    
    if (queue->rear == NULL) {
        queue->front = queue->rear = new_node;
    } else {
        queue->rear->next = new_node;
        queue->rear = new_node;
    }
    
    queue->size++;
}

// Arvore B
void btree_write_node(BTree *tree, BTreeNode *node, int rrn) {
    long offset = sizeof(int) * 2 + rrn * sizeof(BTreeNode);
    fseek(tree->index_file, offset, SEEK_SET);
    fwrite(node, sizeof(BTreeNode), 1, tree->index_file);
    fflush(tree->index_file);
    node->modified = false;
}

void queue_remove_lru(PageQueue *queue, BTree *tree) {
    if (queue->front == NULL) return;
    
    PageQueueNode *lru = queue->front;
    
    if (lru->page->modified) {
        btree_write_node(tree, lru->page, lru->rrn);
    }
    
    queue->front = lru->next;
    if (queue->front == NULL) {
        queue->rear = NULL;
    }
    
    free(lru->page);
    free(lru);
    queue->size--;
}

BTreeNode* btree_read_node(BTree *tree, int rrn) {
    if (rrn < 0) return NULL;
    
    BTreeNode *cached = queue_find(tree->page_queue, rrn);
    if (cached != NULL) {
        queue_move_to_end(tree->page_queue, rrn);
        return cached;
    }
    
    if (queue_is_full(tree->page_queue)) {
        queue_remove_lru(tree->page_queue, tree);
    }
    
    BTreeNode *node = (BTreeNode*)malloc(sizeof(BTreeNode));
    long offset = sizeof(int) * 2 + rrn * sizeof(BTreeNode);
    
    fseek(tree->index_file, offset, SEEK_SET);
    fread(node, sizeof(BTreeNode), 1, tree->index_file);
    node->modified = false;
    
    queue_add(tree->page_queue, node, rrn);
    return node;
}

int btree_allocate_node(BTree *tree) {
    return tree->next_rrn++;
}

void btree_split_child(BTree *tree, BTreeNode *parent, int parent_rrn, int child_index) {
    BTreeNode *full_child = btree_read_node(tree, parent->children[child_index]);
    
    BTreeNode *new_child = (BTreeNode*)calloc(1, sizeof(BTreeNode));
    new_child->is_leaf = full_child->is_leaf;
    new_child->num_keys = MIN_KEYS;
    
    int mid = MIN_KEYS;
    
    for (int i = 0; i < MIN_KEYS; i++) {
        strncpy(new_child->keys[i], full_child->keys[mid + 1 + i], TAMANHO_PLACA);
        new_child->rrns[i] = full_child->rrns[mid + 1 + i];
    }
    
    if (!full_child->is_leaf) {
        for (int i = 0; i <= MIN_KEYS; i++) {
            new_child->children[i] = full_child->children[mid + 1 + i];
        }
    }
    
    full_child->num_keys = MIN_KEYS;
    
    for (int i = parent->num_keys; i > child_index; i--) {
        parent->children[i + 1] = parent->children[i];
    }
    
    int new_child_rrn = btree_allocate_node(tree);
    parent->children[child_index + 1] = new_child_rrn;
    
    for (int i = parent->num_keys - 1; i >= child_index; i--) {
        strncpy(parent->keys[i + 1], parent->keys[i], TAMANHO_PLACA);
        parent->rrns[i + 1] = parent->rrns[i];
    }
    
    strncpy(parent->keys[child_index], full_child->keys[mid], TAMANHO_PLACA);
    parent->rrns[child_index] = full_child->rrns[mid];
    parent->num_keys++;
    
    parent->modified = true;
    full_child->modified = true;
    new_child->modified = true;
    
    btree_write_node(tree, full_child, parent->children[child_index]);
    
    if (queue_is_full(tree->page_queue)) {
        queue_remove_lru(tree->page_queue, tree);
    }
    queue_add(tree->page_queue, new_child, new_child_rrn);
}

void btree_insert_internal(BTree *tree, BTreeNode *node, int node_rrn, const char *placa, int data_rrn) {
    int i = node->num_keys - 1;
    
    if (node->is_leaf) {
        while (i >= 0 && strcmp(placa, node->keys[i]) < 0) {
            strncpy(node->keys[i + 1], node->keys[i], TAMANHO_PLACA);
            node->rrns[i + 1] = node->rrns[i];
            i--;
        }
        
        strncpy(node->keys[i + 1], placa, TAMANHO_PLACA);
        node->rrns[i + 1] = data_rrn;
        node->num_keys++;
        node->modified = true;
    } else {
        while (i >= 0 && strcmp(placa, node->keys[i]) < 0) {
            i--;
        }
        i++;
        
        BTreeNode *child = btree_read_node(tree, node->children[i]);
        
        if (child->num_keys == MAX_KEYS) {
            btree_split_child(tree, node, node_rrn, i);
            
            if (strcmp(placa, node->keys[i]) > 0) {
                i++;
            }
            child = btree_read_node(tree, node->children[i]);
        }
        
        btree_insert_internal(tree, child, node->children[i], placa, data_rrn);
    }
}

void btree_insert(BTree *tree, const char *placa, int data_rrn) {
    if (tree->root_rrn == -1) {
        BTreeNode *root = (BTreeNode*)calloc(1, sizeof(BTreeNode));
        strncpy(root->keys[0], placa, TAMANHO_PLACA);
        root->rrns[0] = data_rrn;
        root->num_keys = 1;
        root->is_leaf = true;
        root->parent_rrn = -1;
        root->modified = true;
        
        tree->root_rrn = btree_allocate_node(tree);
        
        if (queue_is_full(tree->page_queue)) {
            queue_remove_lru(tree->page_queue, tree);
        }
        queue_add(tree->page_queue, root, tree->root_rrn);
        
        return;
    }
    
    BTreeNode *root = btree_read_node(tree, tree->root_rrn);
    
    if (root->num_keys == MAX_KEYS) {
        BTreeNode *new_root = (BTreeNode*)calloc(1, sizeof(BTreeNode));
        new_root->is_leaf = false;
        new_root->children[0] = tree->root_rrn;
        
        int new_root_rrn = btree_allocate_node(tree);
        tree->root_rrn = new_root_rrn;
        
        btree_split_child(tree, new_root, new_root_rrn, 0);
        
        if (queue_is_full(tree->page_queue)) {
            queue_remove_lru(tree->page_queue, tree);
        }
        queue_add(tree->page_queue, new_root, new_root_rrn);
        
        btree_insert_internal(tree, new_root, new_root_rrn, placa, data_rrn);
    } else {
        btree_insert_internal(tree, root, tree->root_rrn, placa, data_rrn);
    }
}

int btree_search_internal(BTree *tree, int node_rrn, const char *placa) {
    if (node_rrn == -1) return -1;
    
    BTreeNode *node = btree_read_node(tree, node_rrn);
    
    int i = 0;
    while (i < node->num_keys && strcmp(placa, node->keys[i]) > 0) {
        i++;
    }
    
    if (i < node->num_keys && strcmp(placa, node->keys[i]) == 0) {
        return node->rrns[i];
    }
    
    if (node->is_leaf) {
        return -1;
    }
    
    return btree_search_internal(tree, node->children[i], placa);
}

bool data_read_veiculo(BTree *tree, int rrn, Veiculo *veiculo) {
    long offset = rrn * sizeof(Veiculo);
    fseek(tree->data_file, offset, SEEK_SET);
    size_t read = fread(veiculo, sizeof(Veiculo), 1, tree->data_file);
    return read == 1;
}

void data_print_veiculo(Veiculo *veiculo) {
    printf("\n--- Dados do Veiculo ---\n");
    printf("Placa: %s\n", veiculo->placa);
    printf("Modelo: %s\n", veiculo->modelo);
    printf("Marca: %s\n", veiculo->marca);
    printf("Ano: %d\n", veiculo->ano);
    printf("Categoria: %s\n", veiculo->categoria);
    printf("Quilometragem: %d km\n", veiculo->quilometragem);
    printf("Status: %s\n", veiculo->status);
    printf("------------------------\n\n");
}

void btree_search(BTree *tree, const char *placa) {
    if (tree->root_rrn == -1) {
        printf("Arvore vazia!\n");
        return;
    }
    
    char placa_busca[100];
    strncpy(placa_busca, placa, sizeof(placa_busca) - 1);
    placa_busca[sizeof(placa_busca) - 1] = '\0';
    normalizar_placa(placa_busca);
    
    printf("\nBuscando placa: '%s'\n", placa_busca);
    
    int data_rrn = btree_search_internal(tree, tree->root_rrn, placa_busca);
    
    if (data_rrn == -1) {
        printf("Placa '%s' nao encontrada!\n", placa_busca);
        return;
    }
    
    printf("\nVeiculo encontrado!\n");
    Veiculo veiculo;
    if (data_read_veiculo(tree, data_rrn, &veiculo)) {
        data_print_veiculo(&veiculo);
    }
}

void data_mark_removed(BTree *tree, int rrn) {
    Veiculo veiculo;
    if (data_read_veiculo(tree, rrn, &veiculo)) {
        strcpy(veiculo.status, "*REMOVIDO*");
        long offset = rrn * sizeof(Veiculo);
        fseek(tree->data_file, offset, SEEK_SET);
        fwrite(&veiculo, sizeof(Veiculo), 1, tree->data_file);
        fflush(tree->data_file);
    }
}

void text_append_veiculo(BTree *tree, Veiculo *veiculo, int rrn) {
    fprintf(tree->text_file, "----------------------------------------\n");
    fprintf(tree->text_file, "RNN: %d\n", rrn);
    fprintf(tree->text_file, "Placa: %s\n", veiculo->placa);
    fprintf(tree->text_file, "Modelo: %s\n", veiculo->modelo);
    fprintf(tree->text_file, "Marca: %s\n", veiculo->marca);
    fprintf(tree->text_file, "Ano: %d\n", veiculo->ano);
    fprintf(tree->text_file, "Categoria: %s\n", veiculo->categoria);
    fprintf(tree->text_file, "Quilometragem: %d km\n", veiculo->quilometragem);
    fprintf(tree->text_file, "Status: %s\n", veiculo->status);
    fprintf(tree->text_file, "----------------------------------------\n\n");
    fflush(tree->text_file);
}

void text_rebuild_file(BTree *tree) {
    fclose(tree->text_file);
    tree->text_file = fopen(tree->text_filename, "w");
    
    fprintf(tree->text_file, "========================================\n");
    fprintf(tree->text_file, "   SISTEMA DE LOCACAO DE VEICULOS\n");
    fprintf(tree->text_file, "========================================\n\n");
    
    fseek(tree->data_file, 0, SEEK_END);
    long file_size = ftell(tree->data_file);
    int num_records = file_size / sizeof(Veiculo);
    
    for (int i = 0; i < num_records; i++) {
        Veiculo veiculo;
        if (data_read_veiculo(tree, i, &veiculo)) {
            if (strcmp(veiculo.status, "*REMOVIDO*") != 0) {
                text_append_veiculo(tree, &veiculo, i);
            }
        }
    }
    
    fflush(tree->text_file);
}

bool btree_remove(BTree *tree, const char *placa) {
    if (tree->root_rrn == -1) {
        printf("Arvore vazia!\n");
        return false;
    }
    
    int data_rrn = btree_search_internal(tree, tree->root_rrn, placa);
    
    if (data_rrn == -1) {
        printf("Placa '%s' nao encontrada!\n", placa);
        return false;
    }
    
    data_mark_removed(tree, data_rrn);
    
    BTreeNode *root = btree_read_node(tree, tree->root_rrn);
    
    for (int i = 0; i < root->num_keys; i++) {
        if (strcmp(placa, root->keys[i]) == 0) {
            for (int j = i; j < root->num_keys - 1; j++) {
                strncpy(root->keys[j], root->keys[j + 1], TAMANHO_PLACA);
                root->rrns[j] = root->rrns[j + 1];
            }
            root->num_keys--;
            root->modified = true;
            
            printf("Veiculo removido com sucesso!\n");
            text_rebuild_file(tree);
            return true;
        }
    }
    
    return false;
}

void btree_print_node(BTree *tree, int rrn, int level) {
    if (rrn == -1) return;
    
    BTreeNode *node = btree_read_node(tree, rrn);
    
    for (int i = 0; i < level; i++) printf("  ");
    printf("RNN=%d [", rrn);
    
    for (int i = 0; i < node->num_keys; i++) {
        printf("%s", node->keys[i]);
        if (i < node->num_keys - 1) printf(", ");
    }
    printf("]\n");
    
    if (!node->is_leaf) {
        for (int i = 0; i <= node->num_keys; i++) {
            btree_print_node(tree, node->children[i], level + 1);
        }
    }
}

void btree_print(BTree *tree) {
    if (tree->root_rrn == -1) {
        printf("Arvore vazia!\n");
        return;
    }
    
    printf("\n=== Estrutura da Arvore B ===\n");
    btree_print_node(tree, tree->root_rrn, 0);
    
    printf("\n=== Cache (%d/%d) ===\n", tree->page_queue->size, P);
    PageQueueNode *current = tree->page_queue->front;
    int pos = 1;
    while (current != NULL) {
        printf("%d. RNN=%d %s\n", pos++, current->rrn, 
               current->page->modified ? "[MOD]" : "");
        current = current->next;
    }
    printf("\n");
}

int data_insert_veiculo(BTree *tree, Veiculo *veiculo) {
    fseek(tree->data_file, 0, SEEK_END);
    long pos = ftell(tree->data_file);
    int rrn = pos / sizeof(Veiculo);
    
    fwrite(veiculo, sizeof(Veiculo), 1, tree->data_file);
    fflush(tree->data_file);
    
    text_append_veiculo(tree, veiculo, rrn);
    
    return rrn;
}

void btree_load_from_data_file(BTree *tree) {
    fseek(tree->data_file, 0, SEEK_END);
    long file_size = ftell(tree->data_file);
    
    if (file_size == 0) {
        printf("Arquivo de dados vazio!\n");
        return;
    }
    
    size_t tamanho_registro = sizeof(Veiculo);
    int num_registros = file_size / tamanho_registro;
    
    printf("\nCarregando veiculos do arquivo veiculos.dat...\n");
    printf("Total de registros: %d\n\n", num_registros);
    
    fseek(tree->data_file, 0, SEEK_SET);
    
    int carregados = 0;
    
    for (int rrn = 0; rrn < num_registros; rrn++) {
        Veiculo veiculo;
        memset(&veiculo, 0, sizeof(Veiculo));
        
        size_t registros_lidos = fread(&veiculo, tamanho_registro, 1, tree->data_file);
        
        if (registros_lidos != 1) {
            continue;
        }
        
        veiculo.placa[TAMANHO_PLACA - 1] = '\0';
        veiculo.modelo[TAMANHO_MODELO - 1] = '\0';
        veiculo.marca[TAMANHO_MARCA - 1] = '\0';
        veiculo.categoria[TAMANHO_CATEGORIA - 1] = '\0';
        veiculo.status[TAMANHO_STATUS - 1] = '\0';
        
        normalizar_placa(veiculo.placa);
        
        int placa_valida = 0;
        for (int i = 0; i < TAMANHO_PLACA; i++) {
            if ((veiculo.placa[i] >= 'A' && veiculo.placa[i] <= 'Z') ||
                (veiculo.placa[i] >= '0' && veiculo.placa[i] <= '9')) {
                placa_valida = 1;
                break;
            }
        }
        
        if (!placa_valida) {
            continue;
        }
        
        if (strstr(veiculo.status, "REMOVIDO") != NULL) {
            continue;
        }
        
        btree_insert(tree, veiculo.placa, rrn);
        text_append_veiculo(tree, &veiculo, rrn);
        carregados++;
    }
    
    printf("Veiculos carregados: %d\n\n", carregados);
}

BTree* btree_create(const char *index_file, const char *data_file, const char *text_file) {
    BTree *tree = (BTree*)malloc(sizeof(BTree));
    strcpy(tree->index_filename, index_file);
    strcpy(tree->data_filename, data_file);
    strcpy(tree->text_filename, text_file);
    
    tree->index_file = fopen(index_file, "wb+");
    if (!tree->index_file) {
        free(tree);
        return NULL;
    }
    
    tree->data_file = fopen(data_file, "rb+");
    if (!tree->data_file) {
        tree->data_file = fopen(data_file, "wb+");
    }
    
    tree->text_file = fopen(text_file, "w");
    if (!tree->text_file) {
        fclose(tree->index_file);
        fclose(tree->data_file);
        free(tree);
        return NULL;
    }
    
    fprintf(tree->text_file, "========================================\n");
    fprintf(tree->text_file, "   SISTEMA DE LOCACAO DE VEICULOS\n");
    fprintf(tree->text_file, "========================================\n\n");
    fflush(tree->text_file);
    
    tree->root_rrn = -1;
    tree->next_rrn = 0;
    tree->page_queue = queue_create();
    
    fwrite(&tree->root_rrn, sizeof(int), 1, tree->index_file);
    fwrite(&tree->next_rrn, sizeof(int), 1, tree->index_file);
    fflush(tree->index_file);
    
    printf("Sistema criado! (M=%d, Cache=%d paginas)\n", M, P);
    
    btree_load_from_data_file(tree);
    
    return tree;
}

BTree* btree_load(const char *index_file, const char *data_file, const char *text_file) {
    BTree *tree = (BTree*)malloc(sizeof(BTree));
    strcpy(tree->index_filename, index_file);
    strcpy(tree->data_filename, data_file);
    strcpy(tree->text_filename, text_file);
    
    tree->index_file = fopen(index_file, "rb+");
    if (!tree->index_file) {
        free(tree);
        return NULL;
    }
    
    tree->data_file = fopen(data_file, "rb+");
    if (!tree->data_file) {
        fclose(tree->index_file);
        free(tree);
        return NULL;
    }
    
    tree->text_file = fopen(text_file, "a");
    if (!tree->text_file) {
        fclose(tree->index_file);
        fclose(tree->data_file);
        free(tree);
        return NULL;
    }
    
    fread(&tree->root_rrn, sizeof(int), 1, tree->index_file);
    fread(&tree->next_rrn, sizeof(int), 1, tree->index_file);
    tree->page_queue = queue_create();
    
    printf("Sistema carregado! (Raiz RNN=%d)\n", tree->root_rrn);
    return tree;
}

void btree_close(BTree *tree) {
    if (tree) {
        PageQueueNode *current = tree->page_queue->front;
        while (current != NULL) {
            if (current->page->modified) {
                btree_write_node(tree, current->page, current->rrn);
            }
            current = current->next;
        }
        
        if (tree->index_file) {
            fseek(tree->index_file, 0, SEEK_SET);
            fwrite(&tree->root_rrn, sizeof(int), 1, tree->index_file);
            fwrite(&tree->next_rrn, sizeof(int), 1, tree->index_file);
            fclose(tree->index_file);
        }
        
        if (tree->data_file) {
            fclose(tree->data_file);
        }
        
        if (tree->text_file) {
            fclose(tree->text_file);
        }
        
        queue_destroy(tree->page_queue);
        free(tree);
        printf("Sistema fechado!\n");
    }
}

void menu_principal(BTree *tree) {
    int opcao;
    char buffer[100];
    
    do {
        printf("\n=== SISTEMA DE LOCACAO DE VEICULOS ===\n");
        printf("1. Inserir veiculo\n");
        printf("2. Buscar veiculo por placa\n");
        printf("3. Remover veiculo\n");
        printf("4. Imprimir arvore e cache\n");
        printf("5. Reconstruir arquivo texto\n");
        printf("0. Sair\n");
        printf("Escolha: ");
        
        if (fgets(buffer, sizeof(buffer), stdin) == NULL) break;
        opcao = atoi(buffer);
        
        if (strchr(buffer, '\n') == NULL) {
            limpar_buffer();
        }
        
        switch(opcao) {
            case 1: {
                Veiculo veiculo;
                memset(&veiculo, 0, sizeof(Veiculo));
    
                printf("\n--- Cadastro de Veiculo ---\n");
    
                ler_string(veiculo.placa, TAMANHO_PLACA, "Placa: ");
                ler_string(veiculo.modelo, TAMANHO_MODELO, "Modelo: ");
                ler_string(veiculo.marca, TAMANHO_MARCA, "Marca: ");
                veiculo.ano = ler_inteiro("Ano: ");
                ler_string(veiculo.categoria, TAMANHO_CATEGORIA, "Categoria: ");
                veiculo.quilometragem = ler_inteiro("Quilometragem: ");
                ler_string(veiculo.status, TAMANHO_STATUS, "Status: ");
    
                int rrn = data_insert_veiculo(tree, &veiculo);
                btree_insert(tree, veiculo.placa, rrn);
                printf("Veiculo inserido com sucesso!\n");
                break;
            }
            
            case 2: {
                char placa[100];
                ler_string(placa, sizeof(placa), "Placa: ");
                btree_search(tree, placa);
                break;
            }
            
            case 3: {
                char placa[100];
                ler_string(placa, sizeof(placa), "Placa: ");
                btree_remove(tree, placa);
                break;
            }
            
            case 4:
                btree_print(tree);
                break;
                
            case 5:
                printf("Reconstruindo arquivo texto...\n");
                text_rebuild_file(tree);
                printf("Arquivo veiculos.txt atualizado!\n");
                break;
                
            case 0:
                printf("Salvando e encerrando...\n");
                break;
                
            default:
                printf("Opcao invalida!\n");
        }
    } while(opcao != 0);
}

int main() {
    BTree *tree = NULL;
    int opcao;
    char buffer[100];
    
    printf("=== SISTEMA DE LOCACAO DE VEICULOS ===\n");
    printf("1. Criar novo indice (carrega veiculos.dat existente)\n");
    printf("2. Carregar indice existente\n");
    printf("Escolha: ");
    
    fgets(buffer, sizeof(buffer), stdin);
    opcao = atoi(buffer);
    
    if (opcao == 1) {
        tree = btree_create("btree_M.idx", "veiculos.dat", "veiculos.txt");
    } else {
        tree = btree_load("btree_M.idx", "veiculos.dat", "veiculos.txt");
    }
    
    if (tree) {
        menu_principal(tree);
        btree_close(tree);
    }
    
    return 0;
}