<?php
/**
 * Copyright 2013 hbz NRW (http://www.hbz-nrw.de/)
 *
 * This file is part of regal-drupal.
 *
 * regal-drupal is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * regal-drupal is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with regal-drupal.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * An edoweb api client that acts on local files.
 */
class EdowebAPIClientLocal implements EdowebAPIClientInterface {

  /*
   * Load an entity from the API.
   *
   * @param $entity
   *   The drupal entity to load data into
   */
  public function load($entity) {
    $id = _edoweb_storage_entity_get_id($entity);
    $rdf_data = file_get_contents("/tmp/{$id}.ttl");
    $rdf_parser = new LibRDF_Parser('turtle');
    _edoweb_storage_entity_deserialize_rdf($entity, $rdf_data, $rdf_parser);
    return TRUE;
  }

  /*
   * Save an entity to the API
   *
   * @param $entity
   *   The drupal entity to store data from
   *
   */
  public function save($entity) {
    $json = _edoweb_storage_entity_serialize_jsonld($entity);
    $rdf = _edoweb_storage_entity_serialize_turtle($entity);
    $id = _edoweb_storage_entity_get_id($entity);
    file_put_contents("/tmp/{$id}.ttl", $rdf);
    file_put_contents("/tmp/{$id}.json", $json);
    return TRUE;
  }

  public function delete($entity) {
    $id = _edoweb_storage_entity_get_id($entity);
    unlink("/tmp/{$id}.ttl", $rdf);
    unlink("/tmp/{$id}.json", $json);
    return TRUE;
  }

}
