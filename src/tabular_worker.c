#include "slotted_page.h"
#include "catalog.h"
#include <stdio.h>
#include <stdint.h>

typedef struct {
    uint64_t matching_rows;
    double aggregated_sum;
} PartialAggregate;

// Scans a 4KB slotted page, evaluates predicate filter on specified column, and updates partial aggregate
PartialAggregate process_tabular_page(const char *page_buf, const TableSchema *schema, uint32_t filter_col_idx, double min_val_filter, uint32_t agg_col_idx) {
    PartialAggregate agg = {0, 0.0};
    const PageHeader *hdr = (const PageHeader *)page_buf;

    const ColumnDescriptor *filter_col = &schema->columns[filter_col_idx];
    const ColumnDescriptor *agg_col = &schema->columns[agg_col_idx];

    for (uint16_t i = 0; i < hdr->slot_count; i++) {
        uint16_t len = 0;
        const char *tuple = slotted_page_get_tuple(page_buf, i, &len);
        if (!tuple) continue;

        // Extract filter column value (assuming INT32 or FLOAT64)
        double filter_val = 0.0;
        if (filter_col->type == TYPE_INT32) {
            int32_t val = *(const int32_t *)(tuple + filter_col->offset);
            filter_val = (double)val;
        } else if (filter_col->type == TYPE_FLOAT64) {
            filter_val = *(const double *)(tuple + filter_col->offset);
        }

        // Apply predicate (e.g., WHERE filter_col > min_val_filter)
        if (filter_val > min_val_filter) {
            // Extract aggregate column value
            double agg_val = 0.0;
            if (agg_col->type == TYPE_INT32) {
                agg_val = (double)*(const int32_t *)(tuple + agg_col->offset);
            } else if (agg_col->type == TYPE_FLOAT64) {
                agg_val = *(const double *)(tuple + agg_col->offset);
            }

            agg.matching_rows++;
            agg.aggregated_sum += agg_val;
        }
    }

    return agg;
}
