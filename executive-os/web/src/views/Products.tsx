import { useEffect, useState } from "react";
import { api } from "../api";
import { Card, ErrorBox, Spinner } from "../components/ui";

interface Product {
  id: string;
  name: string;
  status: string;
  category?: string;
}

export function ProductsView() {
  const [products, setProducts] = useState<Product[] | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    api.products()
      .then(setProducts)
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, []);

  return (
    <Card title="Product Catalog (from specs/catalog.md)">
      <p className="small muted mb">Seeded from the real catalog. Statuses reflect pipeline position.</p>
      {error && <ErrorBox message={error} />}
      {!products ? <Spinner /> : (
        <table>
          <thead><tr><th>#</th><th>Product</th><th>Category</th><th>Status</th></tr></thead>
          <tbody>
            {products.map((p) => (
              <tr key={p.id}>
                <td data-label="#" className="mono faint">{p.id.replace("prod_", "")}</td>
                <td data-label="Product">{p.name}</td>
                <td data-label="Category" className="faint">{p.category ?? "—"}</td>
                <td data-label="Status"><span className="pill">{p.status}</span></td>
              </tr>
            ))}
          </tbody>
        </table>
      )}
    </Card>
  );
}
