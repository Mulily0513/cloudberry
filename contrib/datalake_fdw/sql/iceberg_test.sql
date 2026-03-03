-- DDL
CREATE FOREIGN TABLE iceberg_products (
    id INT,
    product_name TEXT,
    category TEXT,
    price NUMERIC(10, 2),
    stock_date DATE
)
SERVER hive_server 
OPTIONS (filepath 'icebergdb.products', catalog_type 'hive', format 'iceberg', server_name 'hive-cluster-1', table_identifier 'icebergdb.products');

CREATE TABLE local_inventory (
    inventory_id SERIAL PRIMARY KEY,
    product_id INT,
    warehouse_location TEXT,
    quantity INT
);

-- Insert
INSERT INTO iceberg_products (id, product_name, category, price, stock_date)
VALUES (1, 'Laptop Pro', 'Electronics', 1299.99, '2023-10-26');

INSERT INTO iceberg_products (id, product_name, category, price, stock_date)
VALUES
    (2, 'Wireless Mouse', 'Accessories', 75.50, '2023-10-25'),
    (3, 'Mechanical Keyboard', 'Accessories', 150.00, '2023-10-25'),
    (4, '4K Monitor', 'Electronics', 799.00, '2023-10-24'),
    (5, 'USB-C Hub', NULL, 49.99, '2023-10-26');


SELECT count(*) FROM iceberg_products;

SELECT product_name FROM iceberg_products WHERE id = 1;

SELECT * FROM iceberg_products ORDER BY id;

SELECT id, product_name FROM iceberg_products WHERE category = 'Electronics';
SELECT id, product_name FROM iceberg_products WHERE price > 200;
SELECT id, product_name FROM iceberg_products WHERE stock_date = '2023-10-26';
SELECT id, product_name FROM iceberg_products WHERE category IS NULL;

SELECT product_name, price FROM iceberg_products WHERE category = 'Accessories' AND price < 100;
SELECT category, count(*), avg(price) FROM iceberg_products GROUP BY category ORDER BY category;

-- Update
UPDATE iceberg_products SET price = 1249.99 WHERE id = 1;
UPDATE iceberg_products SET category = 'Peripherals' WHERE category = 'Accessories';

SELECT price FROM iceberg_products WHERE id = 1;
SELECT count(*) FROM iceberg_products WHERE category = 'Peripherals';

-- Delete
DELETE FROM iceberg_products WHERE id = 5;
SELECT * FROM iceberg_products WHERE id = 5;

-- Join
INSERT INTO local_inventory (product_id, warehouse_location, quantity)
VALUES
    (1, 'Warehouse A', 50),
    (2, 'Warehouse B', 200),
    (4, 'Warehouse A', 30),
    (99, 'Warehouse C', 10);

SELECT
    p.product_name,
    p.price,
    i.warehouse_location,
    i.quantity
FROM
    iceberg_products p
JOIN
    local_inventory i ON p.id = i.product_id
ORDER BY p.product_name;

SELECT
    p.id,
    p.product_name,
    i.quantity
FROM
    iceberg_products p
LEFT JOIN
    local_inventory i ON p.id = i.product_id
ORDER BY p.id;

SELECT
    p.product_name,
    i.product_id,
    i.warehouse_location
FROM
    iceberg_products p
RIGHT JOIN
    local_inventory i ON p.id = i.product_id
ORDER BY i.product_id;

SELECT
    p.product_name,
    p.price,
    i.warehouse_location
FROM
    iceberg_products p
JOIN
    local_inventory i ON p.id = i.product_id
WHERE
    i.warehouse_location = 'Warehouse A' AND p.price < 1000;

DROP TABLE local_inventory;
DROP FOREIGN TABLE iceberg_products;